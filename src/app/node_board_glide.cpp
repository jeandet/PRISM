#include "node_board_parts.hpp"

#include <prism/app/widget_tree.hpp>
#include <prism/nodes/tidy.hpp>

#include <algorithm>
#include <chrono>
#include <map>

namespace prism::app::node_board_detail {

namespace {

constexpr auto glide_duration = std::chrono::milliseconds(200);
constexpr auto glide_frame = std::chrono::milliseconds(16);

float ease_out_cubic(float t) {
    const float rest = 1.f - t;
    return 1.f - rest * rest * rest;
}

core::Point between(core::Point from, core::Point to, float t) {
    return {core::X{from.x.raw() + (to.x.raw() - from.x.raw()) * t},
            core::Y{from.y.raw() + (to.y.raw() - from.y.raw()) * t}};
}

Glide glide_to_tidy(const nodes::Graph& graph, const NodeBoardParts& parts) {
    std::map<nodes::NodeId, core::Size> sizes;
    for (const auto& [id, card] : parts.cards)
        if (const auto size = parts.tree->arranged_extent(card)) sizes.emplace(id, *size);
    Glide glide{.start = parts.tree->now(), .legs = {}};
    for (const auto& [id, to] : nodes::tidy_layout(graph, sizes)) glide.legs.emplace(id, std::pair{graph.position(id), to});
    return glide;
}

} // namespace

// One frame of the glide: every node still gliding goes where `now` puts it. Each move bumps
// `layout`, which wakes the board for the next frame; redraw_at is the safety net for a frame
// that moves nothing (every node grabbed or gone), so the glide still ends.
void glide_on(nodes::Graph& graph, NodeBoardParts& parts) {
    if (!parts.glide) return;
    const auto now = parts.tree->now();
    const float t = std::clamp(std::chrono::duration<float>(now - parts.glide->start) / glide_duration, 0.f, 1.f);
    for (const auto& [id, leg] : parts.glide->legs)
        if (std::ranges::contains(graph.nodes(), id)) graph.move(id, between(leg.first, leg.second, ease_out_cubic(t)));
    if (t < 1.f) parts.tree->redraw_at(parts.board_id, now + glide_frame);
    else parts.glide.reset();
}

// A tidy starts once every card has a size that two passes agree on: a node added in the same
// turn has no card yet, and a node with no size would be laid out as a point. A card that never
// comes (after fit_waits_limit passes) is laid out as a point rather than waited for forever.
// Returns whether the glide started, which then takes its first step in the same pass.
bool tidy_as_asked(nodes::Graph& graph, NodeBoardParts& parts, std::size_t cards, bool stable) {
    if (!graph.take_tidy_request()) return false;
    const bool carded = cards == graph.nodes().size();
    if (stable && (carded || ++parts.tidy_waits >= fit_waits_limit)) {
        parts.tidy_waits = 0;
        parts.glide = glide_to_tidy(graph, parts);
        return true;
    }
    graph.request_tidy();
    return false;
}

} // namespace prism::app::node_board_detail
