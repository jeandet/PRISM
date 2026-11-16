#include <prism/app/backend.hpp>

namespace prism::app {

// The key function: BackendBase's vtable and type info live where it is defined, and libprism
// needs them (model_app.cpp), so here rather than in the software backend's library.
BackendBase::~BackendBase() = default;

} // namespace prism::app
