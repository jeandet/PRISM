// prism-trace in the browser (trace.html): the same reader and JSON writer as the CLI, behind a
// C interface the page calls. The page owns the serial port (Web Serial); this file only turns
// bytes into a trace.
#include "../chrome_json.hpp"
#include "../trace_reader.hpp"

#include <emscripten/emscripten.h>

#include <cstddef>
#include <span>
#include <string>

namespace {

std::string json;
std::string report;

std::span<const std::byte> as_span(const unsigned char* data, std::size_t size)
{
    return {reinterpret_cast<const std::byte*>(data), size};
}

std::string report_of(const prism_trace::Trace& trace)
{
    std::string text = trace.found ? prism_trace::summary(trace) : "no profile dump found";
    for (const std::string& warning : trace.warnings)
        text += "\nwarning: " + warning;
    return text;
}

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE int prism_trace_dump_complete(const unsigned char* data, std::size_t size)
{
    return prism_trace::dump_complete(as_span(data, size)) ? 1 : 0;
}

// Reads one dump out of the bytes. Returns 1 when one was found; then prism_trace_json() holds
// the Chrome trace JSON. prism_trace_report() is the summary and the warnings, either way.
EMSCRIPTEN_KEEPALIVE int prism_trace_convert(const unsigned char* data, std::size_t size)
{
    const prism_trace::Trace trace = prism_trace::read_trace(as_span(data, size));
    report = report_of(trace);
    json = trace.found ? prism_trace::to_chrome_json(trace) : std::string{};
    return trace.found ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE const char* prism_trace_json() { return json.data(); }
EMSCRIPTEN_KEEPALIVE std::size_t prism_trace_json_size() { return json.size(); }
EMSCRIPTEN_KEEPALIVE const char* prism_trace_report() { return report.c_str(); }

} // extern "C"
