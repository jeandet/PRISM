// The writer half of prism-trace: the Trace as Chrome trace-event JSON
// ({"traceEvents":[...]} -- one function per event kind). Strings are escaped (quotes,
// backslashes, control characters) and sanitised to valid UTF-8 -- the site's name, function
// and file come from the device, and a name cut at 16 bytes can end mid-sequence -- so the
// output is valid JSON whatever the names hold. Every number goes through to_chars, so ts is
// written in its shortest round-trip form. ts is microseconds: ticks / ticks_per_second * 1e6,
// as a double.
#include "chrome_json.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace prism_trace {
namespace {

using prism::profile::Kind;

// The length in bytes (1-4) of the well-formed UTF-8 sequence starting at s[i], or 0 when the
// byte there does not start one (RFC 3629: continuation bytes in 0x80-0xBF, no overlongs, no
// surrogates, nothing past U+10FFFF, no sequence cut off at the end).
std::size_t utf8_sequence(std::string_view s, std::size_t i)
{
    const auto b0 = static_cast<unsigned char>(s[i]);
    if (b0 < 0x80)
        return 1;
    std::size_t len = 0;
    unsigned char lo = 0x80, hi = 0xBF; // the first continuation byte's allowed range
    if (b0 >= 0xC2 && b0 <= 0xDF) {
        len = 1;
    } else if (b0 == 0xE0) {
        len = 2;
        lo = 0xA0; // no overlong 3-byte forms
    } else if (b0 >= 0xE1 && b0 <= 0xEC) {
        len = 2;
    } else if (b0 == 0xED) {
        len = 2;
        hi = 0x9F; // no surrogates
    } else if (b0 >= 0xEE && b0 <= 0xEF) {
        len = 2;
    } else if (b0 == 0xF0) {
        len = 3;
        lo = 0x90; // no overlong 4-byte forms
    } else if (b0 >= 0xF1 && b0 <= 0xF3) {
        len = 3;
    } else if (b0 == 0xF4) {
        len = 3;
        hi = 0x8F; // nothing past U+10FFFF
    } else {
        return 0; // 0x80-0xC1, 0xF5-0xFF: a stray continuation or an invalid lead byte
    }
    if (i + len >= s.size())
        return 0; // the sequence is cut at the end
    const auto b1 = static_cast<unsigned char>(s[i + 1]);
    if (b1 < lo || b1 > hi)
        return 0;
    for (std::size_t k = 2; k <= len; ++k) {
        const auto bk = static_cast<unsigned char>(s[i + k]);
        if (bk < 0x80 || bk > 0xBF)
            return 0;
    }
    return len + 1;
}

// Appends a JSON string (with its quotes), escaping what JSON requires. JSON strings are UTF-8,
// and the device's are not guaranteed to be (a 16-byte thread name can end mid-sequence): valid
// sequences are copied, each invalid byte becomes U+FFFD, so the output is always valid UTF-8.
void append_string(std::string& out, std::string_view value)
{
    constexpr char hex[] = "0123456789abcdef";
    constexpr std::string_view replacement{"\xEF\xBF\xBD"}; // U+FFFD, the replacement character
    out += '"';
    for (std::size_t i = 0; i < value.size();) {
        const auto uc = static_cast<unsigned char>(value[i]);
        if (uc < 0x80) {
            switch (value[i]) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (uc < 0x20) {
                    out += "\\u00";
                    out += hex[uc >> 4];
                    out += hex[uc & 0xF];
                } else {
                    out += value[i];
                }
            }
            ++i;
            continue;
        }
        if (const std::size_t seq = utf8_sequence(value, i); seq != 0) {
            out.append(value.substr(i, seq)); // a valid multi-byte sequence: copied as-is
            i += seq;
        } else {
            out += replacement; // an invalid byte: U+FFFD
            ++i;
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

// One thread's events: begins as "B", each end matched against the nearest open begin with the
// same site (an end with no such begin -- its "B" was overwritten in the ring -- is an orphan
// "E" and is dropped), instants and frames as "i" with "s":"t", counters as "C"; a "B" still
// open when the thread's events end is closed at its last timestamp.
void append_thread_events(std::string& out, bool& first, const Trace& trace, const Trace::Thread& thread)
{
    std::vector<const TraceEvent*> open;
    for (const TraceEvent& event : thread.events) {
        switch (event.kind) {
        case Kind::begin:
            open.push_back(&event);
            append_begin(out, first, trace, thread, event);
            break;
        case Kind::end: {
            // Close the nearest open begin with the same site (a lost middle events chunk can
            // leave ends out of order); an end with no open begin of its site is an orphan --
            // its begin was overwritten in the ring -- dropped.
            std::size_t k = open.size();
            while (k > 0 && open[k - 1]->site != event.site)
                --k;
            if (k > 0) {
                const TraceEvent* begin = open[k - 1];
                open.resize(k - 1); // pop down to (and including) the matching begin
                append_end(out, first, trace, thread, *begin, event.ticks);
            }
            break;
        }
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
