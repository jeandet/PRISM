#pragma once

#include <prism/core/connection.hpp>
#include <prism/core/derived.hpp>
#include <prism/core/field.hpp>

#include <any>
#include <cassert>
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

template <class T>
TypeKey type_key() { return type_key(PortType<T>::name); }

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
