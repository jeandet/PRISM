#include <prism/nodes/snap.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace prism::nodes {

namespace snap_detail {

// The lines a card's leading edge, middle and trailing edge sit on, along one axis.
std::array<float, 3> lines(float start, float extent) {
    return {start, start + extent / 2.f, start + extent};
}

struct Candidate {
    float by;          // the shift that would line the cards up
    float line;        // the coordinate they would share
    bool wire;
    const core::Rect* card; // null for a wire straightening
    float other_x;          // the other end's x, for a wire straightening
};

void add_alignments(std::vector<Candidate>& out, const std::array<float, 3>& mine,
                    const std::array<float, 3>& theirs, const core::Rect* card) {
    for (float from : mine)
        for (float to : theirs)
            out.push_back({to - from, to, false, card, 0.f});
}

std::vector<Candidate> x_candidates(float x, float w, std::span<const core::Rect> others) {
    std::vector<Candidate> out;
    for (const core::Rect& r : others)
        add_alignments(out, lines(x, w), lines(r.origin.x.raw(), r.extent.w.raw()), &r);
    return out;
}

std::vector<Candidate> y_candidates(float y, float h, std::span<const core::Rect> others,
                                    std::span<const LevelWith> wires) {
    std::vector<Candidate> out;
    for (const core::Rect& r : others)
        add_alignments(out, lines(y, h), lines(r.origin.y.raw(), r.extent.h.raw()), &r);
    for (const LevelWith& w : wires)
        out.push_back({w.other_end.raw() - (y + w.port_from_top.raw()), w.other_end.raw(), true,
                       nullptr, w.other_x.raw()});
    return out;
}

// The nearest candidate within `reach`; a wire straightening wins a tie against an alignment.
// simplify: two alignments at the same distance keep the first seen, so which guide shows follows
// the order `others` came in; break the tie by card distance instead if the guide ever jumps.
std::optional<Candidate> pick(std::span<const Candidate> candidates, float reach) {
    std::optional<Candidate> best;
    for (const Candidate& c : candidates) {
        const float distance = std::abs(c.by);
        if (distance > reach) continue;
        const float best_distance = best ? std::abs(best->by) : 0.f;
        const bool better = !best || distance < best_distance
            || (distance == best_distance && c.wire && !best->wire);
        if (better) best = c;
    }
    return best;
}

// The shared vertical line, from the higher card's top to the lower one's bottom.
Guide vertical(float line, core::Point at, core::Size size, const core::Rect& other) {
    const float top = std::min(at.y.raw(), other.origin.y.raw());
    const float bottom = std::max(at.y.raw() + size.h.raw(), other.origin.y.raw() + other.extent.h.raw());
    return {{core::X{line}, core::Y{top}}, {core::X{line}, core::Y{bottom}}};
}

// The shared horizontal line, from the left card's left to the right one's right.
Guide horizontal(float line, core::Point at, core::Size size, const core::Rect& other) {
    const float left = std::min(at.x.raw(), other.origin.x.raw());
    const float right = std::max(at.x.raw() + size.w.raw(), other.origin.x.raw() + other.extent.w.raw());
    return {{core::X{left}, core::Y{line}}, {core::X{right}, core::Y{line}}};
}

// A straightening line runs from the card to the other end of the wire.
Guide straightening(float line, core::Point at, core::Size size, float other_x) {
    const float left = std::min(at.x.raw(), other_x);
    const float right = std::max(at.x.raw() + size.w.raw(), other_x);
    return {{core::X{left}, core::Y{line}}, {core::X{right}, core::Y{line}}};
}

} // namespace snap_detail

Snapped snap(core::Point at, core::Size size, std::span<const core::Rect> others,
             std::span<const LevelWith> wires, float reach) {
    const auto x = snap_detail::pick(snap_detail::x_candidates(at.x.raw(), size.w.raw(), others), reach);
    const auto y = snap_detail::pick(
        snap_detail::y_candidates(at.y.raw(), size.h.raw(), others, wires), reach);

    const core::Point placed = at
        + core::Offset{core::DX{x ? x->by : 0.f}, core::DY{y ? y->by : 0.f}};

    std::vector<Guide> guides;
    if (x) guides.push_back(snap_detail::vertical(x->line, placed, size, *x->card));
    if (y && !y->wire) guides.push_back(snap_detail::horizontal(y->line, placed, size, *y->card));
    if (y && y->wire)
        guides.push_back(snap_detail::straightening(y->line, placed, size, y->other_x));
    return {placed, guides};
}

} // namespace prism::nodes
