#include <prism/nodes/port.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <mutex>
#include <optional>
#include <utility>

namespace prism::nodes {

namespace {

struct Entry {
    TypeKey key;
    std::size_t slot;
    std::function<std::string(const std::any&)> formatter;
};

using KeyMap = std::map<std::string, Entry, std::less<>>;
using Node = KeyMap::value_type;

struct Registry {
    std::mutex mutex;
    KeyMap keys;
    std::vector<Node*> by_key;
    std::size_t app_types = 0;
};

Registry& registry() {
    static Registry r;
    return r;
}

Node* node_at(Registry& r, TypeKey key) {
    return key >= 1 && key <= r.by_key.size() ? r.by_key[key - 1] : nullptr;
}

std::optional<std::size_t> builtin_slot(std::string_view name) {
    constexpr std::array<std::pair<std::string_view, std::size_t>, 5> slots{{
        {"waveform", 0},
        {"number", 1},
        {"boolean", 2},
        {"integer", 3},
        {"text", 4},
    }};
    for (const auto& [type, slot] : slots) {
        if (type == name) return slot;
    }
    return std::nullopt;
}

std::string quote_and_cut(const std::string& text) {
    constexpr std::size_t shown = 24;
    if (text.size() <= shown) return '"' + text + '"';
    std::size_t cut = shown;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    return '"' + text.substr(0, cut) + "…";
}

std::string waveform_text(const std::vector<double>& samples) {
    double peak = 0.0;
    for (const double sample : samples) {
        peak = std::max(peak, std::abs(sample));
    }
    return std::format("{} samples · peak {:.2f}", samples.size(), peak);
}

std::function<std::string(const std::any&)> builtin_formatter(std::string_view name) {
    if (name == "number") return [](const std::any& v) { return std::format("{:.3g}", std::any_cast<double>(v)); };
    if (name == "integer") return [](const std::any& v) { return std::format("{}", std::any_cast<int>(v)); };
    if (name == "boolean") return [](const std::any& v) { return std::string(std::any_cast<bool>(v) ? "true" : "false"); };
    if (name == "text") return [](const std::any& v) { return quote_and_cut(std::any_cast<const std::string&>(v)); };
    if (name == "waveform") return [](const std::any& v) { return waveform_text(std::any_cast<const std::vector<double>&>(v)); };
    return {};
}

TypeKey register_type(Registry& r, std::string_view name) {
    std::size_t slot = 0;
    if (const auto builtin = builtin_slot(name)) slot = *builtin;
    else slot = (wire_slots - 1 + r.app_types++) % wire_slots;

    const auto key = static_cast<TypeKey>(r.by_key.size() + 1);
    auto it = r.keys.emplace(std::string(name), Entry{key, slot, builtin_formatter(name)}).first;
    r.by_key.push_back(&*it);
    return key;
}

} // namespace

TypeKey type_key(std::string_view name) {
    auto& r = registry();
    std::lock_guard lock(r.mutex);
    const auto it = r.keys.find(name);
    return it != r.keys.end() ? it->second.key : register_type(r, name);
}

std::string_view type_name(TypeKey key) {
    auto& r = registry();
    std::lock_guard lock(r.mutex);
    const Node* node = node_at(r, key);
    return node ? std::string_view(node->first) : std::string_view{};
}

std::size_t wire_slot(TypeKey key) {
    auto& r = registry();
    std::lock_guard lock(r.mutex);
    const Node* node = node_at(r, key);
    return node ? node->second.slot : 0;
}

void set_formatter(TypeKey key, std::function<std::string(const std::any&)> formatter) {
    auto& r = registry();
    std::lock_guard lock(r.mutex);
    if (Node* node = node_at(r, key)) node->second.formatter = std::move(formatter);
}

std::string format_value(TypeKey key, const std::any& value) {
    auto& r = registry();
    std::function<std::string(const std::any&)> formatter;
    std::string name;
    {
        std::lock_guard lock(r.mutex);
        const Node* node = node_at(r, key);
        if (!node) return {};
        name = node->first;
        formatter = node->second.formatter;
    }
    // The formatter runs outside the mutex: it is app code and may call back into this registry.
    return formatter && value.has_value() ? formatter(value) : name;
}

} // namespace prism::nodes
