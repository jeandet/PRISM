// The writer half of prism-trace: the Trace as Chrome trace-event JSON
// ({"traceEvents":[...]} -- one function per event kind). Strings are escaped (quotes,
// backslashes, control characters -- the site's name, function and file come from the device)
// and every number goes through to_chars, so the output is valid JSON whatever the names hold.
// ts is microseconds: ticks / ticks_per_second * 1e6, as a double.
#include "chrome_json.hpp"

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace prism_trace {
namespace {

using prism::profile::Kind;

// Appends a JSON string (with its quotes), escaping what JSON requires.
void append_string(std::string& out, std::string_view value)
{
    constexpr char hex[] = "0123456789abcdef";
    out += '"';
    for (const char c : value) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                const auto uc = static_cast<unsigned char>(c);
                out += "\\u00";
                out += hex[uc >> 4];
                out += hex[uc & 0xF];
            } else {
                out += c; // bytes >= 0x80 pass through: JSON strings are UTF-8
            }
        }
    }
    out += '"';
}

// Appends a number in its shortest round-trip form, so ts keeps its full precision.
template <typename T>
void append_number(std::string& out, T value)
{
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
    out.append(buffer, result.ptr);
}

// One "key":"string" inside an object being built; `first` tracks the commas.
void field_string(std::string& out, bool& first, std::string_view key, std::string_view value)
{
    if (!first)
        out += ',';
    first = false;
    append_string(out, key);
    out += ':';
    append_string(out, value);
}

// One "key":number inside an object being built.
template <typename T>
void field_number(std::string& out, bool& first, std::string_view key, T value)
{
    if (!first)
        out += ',';
    first = false;
    append_string(out, key);
    out += ':';
    append_number(out, value);
}

void begin_event(std::string& out, bool& first)
{
    if (!first)
        out += ',';
    first = false;
    out += '{';
}

void end_event(std::string& out) { out += '}'; }

// The "args":{...} sub-object: open_args/close_args around its fields.
void open_args(std::string& out, bool& first)
{
    if (!first)
        out += ',';
    first = false;
    append_string(out, "args");
    out += ":{";
}

void close_args(std::string& out) { out += '}'; }

// ts in microseconds: ticks / ticks_per_second * 1e6.
double ts_us(const Trace& trace, std::uint64_t ticks)
{
    // simplify: a dump without a header chunk has no ticks per second; treat its ticks as us
    const double tps = trace.ticks_per_second > 0.0 ? trace.ticks_per_second : 1.0;
    return static_cast<double>(ticks) * 1e6 / tps;
}

const SiteInfo* find_site(const Trace& trace, std::uint64_t pointer)
{
    const auto it = trace.sites.find(pointer);
    return it != trace.sites.end() ? &it->second : nullptr;
}

// An event's name: its site's name -- except a frame boundary, which is always "frame".
std::string event_name(const Trace& trace, const TraceEvent& event)
{
    if (event.kind == Kind::frame)
        return "frame";
    const SiteInfo* site = find_site(trace, event.site);
    return site != nullptr ? site->name : std::string{};
}

// "args":{"function":..,"file":..,"line":..} -- what a "B" and an "i" carry (the frame site's
// function may be empty).
void append_site_args(std::string& out, bool& first, const Trace& trace, std::uint64_t pointer)
{
    const SiteInfo* site = find_site(trace, pointer);
    open_args(out, first);
    bool f = true;
    field_string(out, f, "function",
                 site != nullptr ? std::string_view{site->function} : std::string_view{});
    field_string(out, f, "file", site != nullptr ? std::string_view{site->file} : std::string_view{});
    field_number(out, f, "line", site != nullptr ? site->line : 0u);
    close_args(out);
}

// {"ph":"B","name":..,"pid":1,"tid":..,"ts":..,"args":{function,file,line}}
void append_begin(std::string& out, bool& first, const Trace& trace, const Trace::Thread& thread,
                  const TraceEvent& event)
{
    begin_event(out, first);
    bool f = true;
    field_string(out, f, "ph", "B");
    field_string(out, f, "name", event_name(trace, event));
    field_number(out, f, "pid", 1u);
    field_number(out, f, "tid", thread.id);
    field_number(out, f, "ts", ts_us(trace, event.ticks));
    append_site_args(out, f, trace, event.site);
    end_event(out);
}

// {"ph":"E","name":..,"pid":1,"tid":..,"ts":..} -- named after the "B" it closes.
void append_end(std::string& out, bool& first, const Trace& trace, const Trace::Thread& thread,
                const TraceEvent& begin, std::uint64_t end_ticks)
{
    begin_event(out, first);
    bool f = true;
    field_string(out, f, "ph", "E");
    field_string(out, f, "name", event_name(trace, begin));
    field_number(out, f, "pid", 1u);
    field_number(out, f, "tid", thread.id);
    field_number(out, f, "ts", ts_us(trace, end_ticks));
    end_event(out);
}

// {"ph":"i","s":"t","name":..,"pid":1,"tid":..,"ts":..,"args":{function,file,line}}
void append_instant(std::string& out, bool& first, const Trace& trace, const Trace::Thread& thread,
                    const TraceEvent& event)
{
    begin_event(out, first);
    bool f = true;
    field_string(out, f, "ph", "i");
    field_string(out, f, "s", "t");
    field_string(out, f, "name", event_name(trace, event));
    field_number(out, f, "pid", 1u);
    field_number(out, f, "tid", thread.id);
    field_number(out, f, "ts", ts_us(trace, event.ticks));
    append_site_args(out, f, trace, event.site);
    end_event(out);
}

// {"ph":"C","name":..,"pid":1,"tid":..,"ts":..,"args":{"value":..}}
void append_counter(std::string& out, bool& first, const Trace& trace, const Trace::Thread& thread,
                    const TraceEvent& event)
{
    begin_event(out, first);
    bool f = true;
    field_string(out, f, "ph", "C");
    field_string(out, f, "name", event_name(trace, event));
    field_number(out, f, "pid", 1u);
    field_number(out, f, "tid", thread.id);
    field_number(out, f, "ts", ts_us(trace, event.ticks));
    open_args(out, f);
    bool af = true;
    field_number(out, af, "value", event.value);
    close_args(out);
    end_event(out);
}

// {"ph":"M","name":"thread_name","pid":1,"tid":..,"ts":0,"args":{"name":..}}
void append_thread_name(std::string& out, bool& first, const Trace::Thread& thread)
{
    begin_event(out, first);
    bool f = true;
    field_string(out, f, "ph", "M");
    field_string(out, f, "name", "thread_name");
    field_number(out, f, "pid", 1u);
    field_number(out, f, "tid", thread.id);
    field_number(out, f, "ts", 0.0);
    open_args(out, f);
    bool af = true;
    field_string(out, af, "name", thread.name);
    close_args(out);
    end_event(out);
}

// One thread's events: begins as "B", each end matched against the open begins (an orphan "E" --
// its "B" was overwritten in the ring -- is dropped), instants and frames as "i" with "s":"t",
// counters as "C"; a "B" still open when the thread's events end is closed at its last timestamp.
void append_thread_events(std::string& out, bool& first, const Trace& trace, const Trace::Thread& thread)
{
    std::vector<const TraceEvent*> open;
    for (const TraceEvent& event : thread.events) {
        switch (event.kind) {
        case Kind::begin:
            open.push_back(&event);
            append_begin(out, first, trace, thread, event);
            break;
        case Kind::end:
            if (!open.empty()) {
                const TraceEvent* begin = open.back();
                open.pop_back();
                append_end(out, first, trace, thread, *begin, event.ticks);
            }
            break; // an orphan end: its begin was overwritten -- dropped
        case Kind::instant:
        case Kind::frame:
            append_instant(out, first, trace, thread, event);
            break;
        case Kind::counter:
            append_counter(out, first, trace, thread, event);
            break;
        }
    }
    const std::uint64_t last_ticks = thread.events.empty() ? 0 : thread.events.back().ticks;
    for (const TraceEvent* begin : open) // still open: close at the thread's last timestamp
        append_end(out, first, trace, thread, *begin, last_ticks);
}

} // namespace

std::string to_chrome_json(const Trace& trace)
{
    std::string out = "{\"traceEvents\":[";
    bool first = true;
    for (const Trace::Thread& thread : trace.threads)
        append_thread_events(out, first, trace, thread);
    for (const Trace::Thread& thread : trace.threads) // the thread names as "M" metadata
        append_thread_name(out, first, thread);
    out += "]}";
    return out;
}

} // namespace prism_trace
