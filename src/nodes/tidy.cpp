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
    std::vector<std::size_t> wired;              // column → how many of its cards have wires
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
// simplify: cycles are not broken, so the nodes of one (and all after them) stay at depth 0,
// stacked in the first column. Graph::connect refuses a cycle, so none arises today; if a graph
// can ever be loaded with one, drop the DFS back edges before this.
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
    out.wired.assign(count, 0);
    for (std::size_t i = 0; i < depth.size(); ++i)
        if (has_wires(wiring, i)) {
            out.cards[depth[i]].push_back(i);
            ++out.wired[depth[i]];
        }
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

// Each card's place among `reference`; a card with no neighbour there keeps its own place.
// simplify: that own place is a rank here, not among `reference`, so such cards only hold their
// order against each other; take their key from the other neighbour column if that looks wrong.
std::vector<float> crossings_key(std::span<const std::size_t> column,
                                 const std::vector<std::size_t>& reference,
                                 const Neighbours& neighbours) {
    std::vector<float> out;
    for (std::size_t place = 0; place < column.size(); ++place) {
        std::vector<std::size_t> nearby;
        for (const std::size_t node : neighbours[column[place]]) {
            const auto at = std::ranges::find(reference, node);
            if (at != reference.end())
                nearby.push_back(static_cast<std::size_t>(std::distance(reference.begin(), at)));
        }
        out.push_back(nearby.empty() ? static_cast<float>(place) : median(nearby));
    }
    return out;
}

void order(std::span<std::size_t> column, const std::vector<std::size_t>& reference,
           const Neighbours& neighbours) {
    const std::vector<float> key = crossings_key(column, reference, neighbours);
    std::vector<std::pair<float, std::size_t>> cards;
    for (std::size_t place = 0; place < column.size(); ++place)
        cards.emplace_back(key[place], column[place]);
    std::ranges::stable_sort(cards, {}, [](const auto& card) { return card.first; });
    for (std::size_t place = 0; place < column.size(); ++place) column[place] = cards[place].second;
}

void reduce_crossings(Columns& columns, const Wiring& wiring) {
    // A card with no wires has no place to sort by, so it keeps its place at the column's end.
    const auto wired = [&](std::size_t c) {
        return std::span{columns.cards[c]}.first(columns.wired[c]);
    };
    for (int sweep = 0; sweep < 4; ++sweep) {
        if (sweep % 2 == 0) {
            for (std::size_t c = 1; c < columns.cards.size(); ++c)
                order(wired(c), columns.cards[c - 1], wiring.ins);
            continue;
        }
        for (std::size_t c = columns.cards.size() - 1; c > 0; --c)
            order(wired(c - 1), columns.cards[c], wiring.outs);
    }
}

std::vector<core::X> column_x(const Columns& columns, std::span<const core::Size> sizes,
                              float gap) {
    std::vector<core::X> out(columns.cards.size());
    for (std::size_t c = 1; c < out.size(); ++c) {
        float widest = 0.f;
        for (const std::size_t node : columns.cards[c - 1])
            widest = std::max(widest, sizes[node].w.raw());
        out[c] = out[c - 1] + core::DX{widest + gap};
    }
    return out;
}

std::vector<core::Point> places(const Columns& columns, std::span<const core::Size> sizes,
                                TidySpacing spacing) {
    const std::vector<core::X> x = column_x(columns, sizes, spacing.column_gap);
    std::vector<core::Point> out(sizes.size());
    for (std::size_t c = 0; c < columns.cards.size(); ++c) {
        core::Y y{};
        for (const std::size_t node : columns.cards[c]) {
            out[node] = core::Point{x[c], y};
            y = y + core::DY{sizes[node].h.raw() + spacing.row_gap};
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

// Moves the laid-out cards so their bounding box starts where the cards' did.
std::vector<core::Point> anchored(std::span<const core::Point> placed, core::Point origin) {
    const core::Point start = top_left(placed);
    const core::Offset shift{origin.x - start.x, origin.y - start.y};
    std::vector<core::Point> out;
    out.reserve(placed.size());
    for (const core::Point at : placed) out.push_back(at + shift);
    return out;
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
    const std::vector<core::Point> placed = tidy_detail::anchored(
        tidy_detail::places(columns, cards, spacing), tidy_detail::graph_top_left(graph, ids));

    std::map<NodeId, core::Point> out;
    for (std::size_t i = 0; i < ids.size(); ++i) out[ids[i]] = placed[i];
    return out;
}

} // namespace prism::nodes
