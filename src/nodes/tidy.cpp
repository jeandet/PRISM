#include <prism/nodes/tidy.hpp>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <map>
#include <span>
#include <utility>
#include <vector>

namespace prism::nodes {

namespace tidy_detail {

using Neighbours = std::vector<std::vector<std::size_t>>;

struct Wiring {
    Neighbours ins, outs; // node index → the node indices wired into it, out of it
};

struct Columns {
    std::vector<std::vector<std::size_t>> cards; // column → node indices, top to bottom
};

core::Size size_of(const std::map<NodeId, core::Size>& sizes, NodeId id) {
    const auto at = sizes.find(id);
    return at == sizes.end() ? core::Size{} : at->second;
}

bool has_wires(const Wiring& wiring, std::size_t node) {
    return !wiring.ins[node].empty() || !wiring.outs[node].empty();
}

Wiring read(const Graph& graph, const std::vector<NodeId>& ids) {
    std::map<NodeId, std::size_t> index;
    for (std::size_t i = 0; i < ids.size(); ++i) index[ids[i]] = i;

    Wiring wiring{Neighbours(ids.size()), Neighbours(ids.size())};
    for (const EdgeId edge : graph.edges()) {
        const EdgeInfo& info = *graph.edge(edge);
        const std::size_t from = index.at(info.from.node);
        const std::size_t to = index.at(info.to.node);
        wiring.outs[from].push_back(to);
        wiring.ins[to].push_back(from);
    }
    return wiring;
}

// Kahn's order over the wires: a node's depth is one past its deepest source.
std::vector<std::size_t> depths(const Wiring& wiring) {
    std::vector<std::size_t> pending(wiring.ins.size());
    for (std::size_t i = 0; i < pending.size(); ++i) pending[i] = wiring.ins[i].size();

    std::vector<std::size_t> ready;
    for (std::size_t i = 0; i < pending.size(); ++i)
        if (pending[i] == 0) ready.push_back(i);

    std::vector<std::size_t> depth(wiring.ins.size(), 0);
    for (std::size_t next = 0; next < ready.size(); ++next) {
        const std::size_t from = ready[next];
        for (const std::size_t to : wiring.outs[from]) {
            depth[to] = std::max(depth[to], depth[from] + 1);
            if (--pending[to] == 0) ready.push_back(to);
        }
    }
    return depth;
}

Columns columns_of(const Wiring& wiring, const std::vector<std::size_t>& depth) {
    Columns out;
    const std::size_t count =
        depth.empty() ? 0 : *std::ranges::max_element(depth) + 1;
    out.cards.resize(count);
    for (std::size_t i = 0; i < depth.size(); ++i)
        if (has_wires(wiring, i)) out.cards[depth[i]].push_back(i);
    for (std::size_t i = 0; i < depth.size(); ++i)
        if (!has_wires(wiring, i)) out.cards[depth[i]].push_back(i);
    return out;
}

float median(std::vector<std::size_t> positions) {
    std::ranges::sort(positions);
    const std::size_t middle = positions.size() / 2;
    if (positions.size() % 2 == 1) return static_cast<float>(positions[middle]);
    return (static_cast<float>(positions[middle - 1]) + static_cast<float>(positions[middle])) / 2.f;
}

// Each card's place among `reference`, or its own place when it has no neighbour there.
std::vector<float> crossings_key(const std::vector<std::size_t>& column,
                                 const std::vector<std::size_t>& reference,
                                 const Neighbours& neighbours) {
    std::vector<float> out;
    for (std::size_t place = 0; place < column.size(); ++place) {
        std::vector<std::size_t> near;
        for (const std::size_t node : neighbours[column[place]]) {
            const auto at = std::ranges::find(reference, node);
            if (at != reference.end())
                near.push_back(static_cast<std::size_t>(std::distance(reference.begin(), at)));
        }
        out.push_back(near.empty() ? static_cast<float>(place) : median(near));
    }
    return out;
}

void order(std::vector<std::size_t>& column, const std::vector<std::size_t>& reference,
           const Neighbours& neighbours) {
    const std::vector<float> key = crossings_key(column, reference, neighbours);
    std::vector<std::pair<float, std::size_t>> cards;
    for (std::size_t place = 0; place < column.size(); ++place)
        cards.emplace_back(key[place], column[place]);
    std::ranges::stable_sort(cards, {}, [](const auto& card) { return card.first; });
    for (std::size_t place = 0; place < column.size(); ++place) column[place] = cards[place].second;
}

void reduce_crossings(Columns& columns, const Wiring& wiring) {
    for (int sweep = 0; sweep < 4; ++sweep) {
        if (sweep % 2 == 0) {
            for (std::size_t c = 1; c < columns.cards.size(); ++c)
                order(columns.cards[c], columns.cards[c - 1], wiring.ins);
            continue;
        }
        for (std::size_t c = columns.cards.size() - 1; c > 0; --c)
            order(columns.cards[c - 1], columns.cards[c], wiring.outs);
    }
}

std::vector<float> column_x(const Columns& columns, std::span<const core::Size> sizes,
                            float gap) {
    std::vector<float> out(columns.cards.size(), 0.f);
    for (std::size_t c = 1; c < out.size(); ++c) {
        float widest = 0.f;
        for (const std::size_t node : columns.cards[c - 1])
            widest = std::max(widest, sizes[node].w.raw());
        out[c] = out[c - 1] + widest + gap;
    }
    return out;
}

std::vector<core::Point> places(const Columns& columns, std::span<const core::Size> sizes,
                                TidySpacing spacing) {
    const std::vector<float> x = column_x(columns, sizes, spacing.column_gap);
    std::vector<core::Point> out(sizes.size());
    for (std::size_t c = 0; c < columns.cards.size(); ++c) {
        float y = 0.f;
        for (const std::size_t node : columns.cards[c]) {
            out[node] = core::Point{core::X{x[c]}, core::Y{y}};
            y += sizes[node].h.raw() + spacing.row_gap;
        }
    }
    return out;
}

core::Point top_left(std::span<const core::Point> points) {
    core::Point out = points.front();
    for (const core::Point at : points)
        out = {core::X{std::min(out.x.raw(), at.x.raw())},
               core::Y{std::min(out.y.raw(), at.y.raw())}};
    return out;
}

core::Point graph_top_left(const Graph& graph, std::span<const NodeId> ids) {
    std::vector<core::Point> at;
    for (const NodeId id : ids) at.push_back(graph.position(id));
    return top_left(at);
}

} // namespace tidy_detail

std::map<NodeId, core::Point> tidy_layout(const Graph& graph,
                                          const std::map<NodeId, core::Size>& sizes,
                                          TidySpacing spacing) {
    const std::vector<NodeId> ids = graph.nodes();
    if (ids.empty()) return {};

    const tidy_detail::Wiring wiring = tidy_detail::read(graph, ids);
    tidy_detail::Columns columns =
        tidy_detail::columns_of(wiring, tidy_detail::depths(wiring));
    tidy_detail::reduce_crossings(columns, wiring);

    std::vector<core::Size> cards;
    for (const NodeId id : ids) cards.push_back(tidy_detail::size_of(sizes, id));
    const std::vector<core::Point> placed = tidy_detail::places(columns, cards, spacing);

    // Move the layers so their bounding box starts where the cards' did.
    const core::Point origin = tidy_detail::graph_top_left(graph, ids);
    const core::Point laid = tidy_detail::top_left(placed);
    const core::Offset shift{origin.x - laid.x, origin.y - laid.y};

    std::map<NodeId, core::Point> out;
    for (std::size_t i = 0; i < ids.size(); ++i) out[ids[i]] = placed[i] + shift;
    return out;
}

} // namespace prism::nodes
