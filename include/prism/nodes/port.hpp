#pragma once

#include <prism/core/connection.hpp>
#include <prism/core/derived.hpp>
#include <prism/core/field.hpp>

#include <any>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prism::nodes {

using TypeKey = std::uint32_t;

// A port's value type, named. An app's own type specializes it.
// The name, not the C++ type, makes the key: a Python extension has its own copy of every
// template and must still land on the same key.
template <class T> struct PortType;
template <> struct PortType<double> { static constexpr std::string_view name = "number"; };
template <> struct PortType<int> { static constexpr std::string_view name = "integer"; };
template <> struct PortType<bool> { static constexpr std::string_view name = "boolean"; };
template <> struct PortType<std::string> { static constexpr std::string_view name = "text"; };
template <> struct PortType<std::vector<double>> { static constexpr std::string_view name = "waveform"; };

TypeKey type_key(std::string_view name);
std::string_view type_name(TypeKey key);

// The wire colour slot a type is drawn in: built-ins fixed, other types in registration order.
inline constexpr std::size_t wire_slots = 6;
std::size_t wire_slot(TypeKey key);

// What a probe on a wire of this type shows; without one registered, the type's name.
void set_formatter(TypeKey key, std::function<std::string(const std::any&)> formatter);

// A formatter that only takes hold when the key has none yet: the type's own formatting, which an
// app's set_formatter overrides whichever of the two came first.
void set_default_formatter(TypeKey key, std::function<std::string(const std::any&)> formatter);

std::string format_value(TypeKey key, const std::any& value);

// A PortType<T> carrying `static std::string format(const T&)` registers that formatting the first
// time its key is asked for, so no static initialiser is needed anywhere.
template <class T>
inline constexpr bool formats_probe_text = requires(const T& v) { std::string{PortType<T>::format(v)}; };

template <class T>
void register_probe_format(TypeKey key) {
    set_default_formatter(key, [](const std::any& v) -> std::string {
        const auto* value = std::any_cast<T>(&v);
        return value ? PortType<T>::format(*value) : std::string(PortType<T>::name);
    });
}

template <class T>
TypeKey type_key() {
    const TypeKey key = type_key(PortType<T>::name);
    if constexpr (formats_probe_text<T>) {
        static const bool once = (register_probe_format<T>(key), true);
        (void)once;
    }
    return key;
}

enum class Dir { In, Out };

class Port {
public:
    Port(Dir dir, TypeKey type, std::string name)
        : dir_(dir), type_(type), name_(std::move(name)) {}
    virtual ~Port() = default;
    Port(const Port&) = delete;
    Port& operator=(const Port&) = delete;

    Dir dir() const { return dir_; }
    TypeKey type() const { return type_; }
    std::string_view name() const { return name_; }

    virtual std::any value() const = 0;
    virtual void set_value(const std::any& v) = 0;
    virtual void reset() = 0;
    virtual core::Connection on_change(std::function<void()> cb) = 0;

private:
    Dir dir_;
    TypeKey type_;
    std::string name_;
};

template <class T>
class In : public Port {
public:
    In(std::string name, T fallback = {})
        : Port(Dir::In, type_key<T>(), std::move(name))
        , field(fallback)
        , fallback_(std::move(fallback)) {}

    const T& get() const { return field.get(); }

    std::any value() const override { return field.get(); }
    void set_value(const std::any& v) override { field.set(std::any_cast<T>(v)); }
    void reset() override { field.set(fallback_); }
    core::Connection on_change(std::function<void()> cb) override {
        return field.on_change().connect([cb = std::move(cb)](const T&) { cb(); });
    }

    core::Field<T> field;

private:
    T fallback_;
};

template <class T>
class Out : public Port {
public:
    Out(std::string name, std::function<T()> compute)
        : Port(Dir::Out, type_key<T>(), std::move(name))
        , derived_(std::move(compute)) {}

    const T& get() const { return derived_.get(); }

    std::any value() const override { return derived_.get(); }
    void set_value(const std::any&) override { assert(false && "an output is computed, not set"); }
    void reset() override { assert(false && "an output is computed, not reset"); }
    core::Connection on_change(std::function<void()> cb) override {
        return derived_.on_change().connect([cb = std::move(cb)](const T&) { cb(); });
    }

private:
    core::Derived<T> derived_;
};

} // namespace prism::nodes
