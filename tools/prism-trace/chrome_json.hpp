#pragma once

// The writer half of prism-trace: the Trace as Chrome trace-event JSON
// ({"traceEvents":[...]} -- what ui.perfetto.dev opens, and what Tracy imports). Blocks become
// "B"/"E" (ts in microseconds, pid 1, tid = the thread's id), instants "i" with "s":"t",
// counters "C" with args.value, frame boundaries an instant named "frame", and each thread's
// name a "M" thread_name metadata event. An event's name is its site's name; "B" and "i"
// events carry "args":{"function":..,"file":..,"line":..} (the frame site's function may be
// empty). An orphan "E" -- its "B" was overwritten in the ring -- is dropped, and a "B" still
// open when the thread's events end is closed at the thread's last timestamp.
#include "trace_reader.hpp"

#include <string>

namespace prism_trace {

std::string to_chrome_json(const Trace& trace);

} // namespace prism_trace
