#include <prism/app/backend.hpp>
#include <prism/backends/software_backend.hpp>

namespace prism::app {
using namespace prism::core;

Backend Backend::software(RenderConfig cfg) {
    return Backend{std::make_unique<prism::backends::SoftwareBackend>(cfg)};
}

} // namespace prism::app
