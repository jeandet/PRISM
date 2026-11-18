#include "node_board_parts.hpp"

#include <prism/app/widget_tree.hpp>
#include <prism/nodes/tidy.hpp>
#include <prism/ui/animation.hpp>

#include <algorithm>
#include <chrono>
#include <map>

namespace prism::app::node_board_detail {

namespace {

constexpr auto glide_duration = std::chrono::milliseconds(200);
constexpr auto glide_frame = std::chrono::milliseconds(16);

core::Point glide_point(core::Point from, core::Point to, float t) {
    const ui::EasedProgress eased = ui::ease::out_cubic(ui::Progress{t});
    return {ui::lerp(from.x, to.x, eased), ui::lerp(from.y, to.y, eased)};
}

bool being_dragged(const NodeBoardParts& parts, nodes::NodeId id) {
    return std::ranges::any_of(parts.grips, [id](const NodeGrip& g) { return g.id == id && g.press; });
}

Glide glide_to_tidy(const nodes::Graph& graph, const NodeBoardParts& parts) {
    std::map<nodes::NodeId, core::Size> sizes;
    for (const auto& [id, card] : parts.cards)
        if (const auto size = parts.tree->arranged_extent(card)) sizes.emplace(id, *size);
    const auto start = parts.tree->now();
    Glide glide{.start = start, .next_step = start, .legs = {}};
    for (const auto& [id, to] : nodes::tidy_layout(graph, sizes))
        if (!being_dragged(parts, id)) glide.legs.emplace(id, std::pair{graph.position(id), to});
    return glide;
}

} // namespace

// One step of the glide, at most one per glide_frame: every node still gliding goes where `now`
// puts it. Each move bumps `layout`, which publishes at once, so a step not yet due moves nothing
// and asks for a frame when it is. The same ask is the safety net for a step that moves nothing
// (every node grabbed or gone), so the glide still ends.
void glide_on(nodes::Graph& graph, NodeBoardParts& parts) {
    if (!parts.glide) return;
    const auto now = parts.tree->now();
    if (now < parts.glide->next_step) return parts.tree->redraw_at(parts.board_id, parts.glide->next_step);
    const float t = std::clamp(std::chrono::duration<float>(now - parts.glide->start) / glide_duration, 0.f, 1.f);
    const auto alive = graph.nodes();
    for (const auto& [id, leg] : parts.glide->legs)
        if (std::ranges::contains(alive, id)) graph.move(id, glide_point(leg.first, leg.second, t));
    parts.glide->next_step = now + glide_frame;
    if (t < 1.f) parts.tree->redraw_at(parts.board_id, parts.glide->next_step);
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
