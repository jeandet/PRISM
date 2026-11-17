#include <prism/nodes/port.hpp>

#include <map>
#include <mutex>

namespace prism::nodes {

namespace {

struct Registry {
    std::mutex mutex;
    std::map<std::string, TypeKey, std::less<>> keys;
    std::vector<const std::string*> names;
};

Registry& registry() {
    static Registry r;
    return r;
}

} // namespace

TypeKey type_key(std::string_view name) {
    auto& r = registry();
    std::lock_guard lock(r.mutex);
    auto it = r.keys.find(name);
    if (it == r.keys.end()) {
        it = r.keys.emplace(std::string(name), static_cast<TypeKey>(r.names.size() + 1)).first;
        r.names.push_back(&it->first);
    }
    return it->second;
}

std::string_view type_name(TypeKey key) {
    auto& r = registry();
    std::lock_guard lock(r.mutex);
    return key >= 1 && key <= r.names.size() ? std::string_view(*r.names[key - 1]) : std::string_view{};
}

} // namespace prism::nodes
