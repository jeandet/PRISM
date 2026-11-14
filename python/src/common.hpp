#pragma once
// Shared preamble: the prism, nanobind and std includes, `nb`, and the namespace usings.

#include <prism/backends/settings_dir.hpp>
#include <deque>
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/function.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>

#include <prism/core/field.hpp>
#include <prism/render/image_decoder.hpp>
#include <prism/core/shared.hpp>
#include <prism/core/channel.hpp>
#include <prism/core/connection.hpp>
#include <prism/core/error_hub.hpp>
#include <prism/core/transaction.hpp>
#include <prism/app/model_app.hpp>
#include <prism/app/backend.hpp>
#include <prism/ui/delegate.hpp> // Slider<T>, Checkbox, Orientation — for BoundSliderValue/BoundCheckboxValue
#include <prism/delegates/form_delegates.hpp> // RadioGroup, SpinBox<T>, ProgressBar
#include <prism/widgets/plot.hpp>
#include <prism/ui/tree.hpp>

#include <prism/app/headless_window.hpp>
#include <algorithm>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <map>
#include <optional>
#include <thread>
#include <type_traits>
#include <vector>

namespace nb = nanobind;
using namespace prism::core;
using namespace prism::app;
