#pragma once

#include <prism/core/connection.hpp>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

// The connections a block makes to other blocks' fields, kept as long as the block lives: Qt's
// context object (connect(sender, signal, context, lambda)), as a member instead of a base class.
//
//   core::Wiring wire;                                // dies with the block: so do its connections
//   wire.on(app.theme, [this](const Theme& t) { restyle(t); });
//   wire.on_any([this] { update(); }, name, fruit, copies);
//   wire.link(mixer.level, gain);                     // gain follows mixer.level
//
// A block reacting to its own fields can use field.observe(fn) instead: both die together. The
// other way round is safe too -- a field that dies first leaves its connections inert.
namespace prism::core {

class Wiring {
public:
    Wiring() = default;
    Wiring(const Wiring&) = delete;
    Wiring& operator=(const Wiring&) = delete;
    Wiring(Wiring&&) = default;
    Wiring& operator=(Wiring&&) = default;

    // fn(value) after each change of `source` (a Field, a State, a Derived, ...).
    template <typename Source, typename F>
        requires requires(Source& s) { s.on_change(); }
    void on(Source& source, F&& fn) {
        connections_.push_back(source.on_change().connect(std::forward<F>(fn)));
    }

    // The same for a bare signal.
    template <typename... Args, typename F>
    void on(SenderHub<Args...>& hub, F&& fn) {
        connections_.push_back(hub.connect(std::forward<F>(fn)));
    }

    // fn() after a change of any of `sources`: one reaction to several inputs.
    template <typename F, typename... Sources>
        requires(sizeof...(Sources) > 0)
    void on_any(F&& fn, Sources&... sources) {
        auto shared = std::make_shared<std::decay_t<F>>(std::forward<F>(fn));
        (on(sources, [shared](const auto&...) { (*shared)(); }), ...);
    }

    // target takes source's value now and after each change: one block's output feeds another's
    // input. Two links in opposite directions settle, since setting an equal value emits nothing.
    // The target must outlive this Wiring: keep the Wiring in the target's block.
    template <typename Source, typename Target>
    void link(Source& source, Target& target) {
        link(source, target, [](const auto& v) { return v; });
    }

    // The same through convert(value), which may change the type.
    template <typename Source, typename Target, typename F>
        requires requires(Source& s, Target& t, F& f) { t.set(f(s.get())); }
    void link(Source& source, Target& target, F&& convert) {
        target.set(convert(source.get()));
        on(source, [&target, convert = std::forward<F>(convert)](const auto& v) {
            target.set(convert(v));
        });
    }

    // A connection made some other way (hub | core::then(...), a helper that returns one).
    void keep(Connection connection) { connections_.push_back(std::move(connection)); }

    void clear() { connections_.clear(); }   // disconnects everything now
    [[nodiscard]] std::size_t size() const { return connections_.size(); }

private:
    std::vector<Connection> connections_;
};

} // namespace prism::core
