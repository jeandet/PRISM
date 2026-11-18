#include <prism/ui/context.hpp>

#include <map>
#include <mutex>
#include <optional>

namespace prism::ui {

namespace {

struct Looks {
    Color dark, light;
};

struct Registry {
    std::mutex mutex;
    std::map<std::string, Looks, std::less<>> families;
};

Registry& registry() {
    static Registry r;
    return r;
}

std::optional<Looks> registered(std::string_view family) {
    auto& r = registry();
    std::lock_guard lock(r.mutex);
    const auto it = r.families.find(family);
    return it != r.families.end() ? std::optional{it->second} : std::nullopt;
}

} // namespace

void set_family_color(std::string name, Color dark, Color light) {
    auto& r = registry();
    std::lock_guard lock(r.mutex);
    r.families.insert_or_assign(std::move(name), Looks{dark, light});
}

Color node_family_color(const Theme& t, std::string_view family) {
    if (const auto looks = registered(family)) return t.dark ? looks->dark : looks->light;
    for (const auto& known : t.node_families) {
        if (known.name == family) return known.color;
    }
    return t.node_family_default;
}

} // namespace prism::ui
