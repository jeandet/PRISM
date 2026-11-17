#include <prism/app/node_board.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/nodes/wire_geometry.hpp>

#include <array>
#include <cassert>
#include <memory>

namespace prism::app {

void BoardBackground::canvas(render::DrawList& dl, core::Rect r, const ui::WidgetNode& n) {
    dl.filled_rect(r, n.theme->canvas_bg);
}

std::optional<core::Point> wire_end(const NodeBoardParts& parts, const nodes::Port* port,
                                    const ui::BoardRectOf& rect_of) {
    const auto it = parts.ports.find(port);
    if (it == parts.ports.end()) return std::nullopt;
    const auto card = rect_of(it->second.card);
    const auto row = rect_of(it->second.row);
    if (!card || !row) return std::nullopt;
    const float x = card->origin.x.raw() + (it->second.dir == nodes::Dir::Out ? card->extent.w.raw() : 0.f);
    return core::Point{core::X{x}, core::Y{row->origin.y.raw() + row->extent.h.raw() / 2.f}};
}

namespace {

constexpr float wire_thickness = 2.f;
constexpr float dot_radius = 5.f;

// Wakes the board on either change: the next frame rebuilds it if its nodes changed and lays it
// out again in any case.
std::function<core::Connection(std::function<void()>)> on_either(core::Field<std::uint64_t>& a,
                                                                 core::Field<std::uint64_t>& b) {
    return [&a, &b](std::function<void()> cb) {
        auto links = std::make_shared<std::array<core::Connection, 2>>(std::array{
            a.on_change().connect([cb](std::uint64_t) { cb(); }),
            b.on_change().connect([cb](std::uint64_t) { cb(); })});
        return core::Connection{std::make_shared<std::function<void()>>([links] {
            for (auto& l : *links) l.disconnect();
        })};
    };
}

void draw_wires(render::DrawList& dl, const nodes::Graph& graph, const NodeBoardParts& parts,
                const ui::BoardRectOf& rect_of, Color color) {
    for (const auto id : graph.edges()) {
        const auto* e = graph.edge(id);
        const auto from = wire_end(parts, graph.port(e->from), rect_of);
        const auto to = wire_end(parts, graph.port(e->to), rect_of);
        if (from && to) dl.polyline(nodes::polyline(nodes::wire_curve(*from, *to)), color, wire_thickness);
    }
}

void draw_dots(render::DrawList& dl, const NodeBoardParts& parts, const ui::BoardRectOf& rect_of, Color color) {
    for (const auto& [port, place] : parts.ports)
        if (const auto at = wire_end(parts, port, rect_of)) dl.circle(*at, dot_radius, color);
}

void node_header(ViewBuilder& vb, NodeBoardParts& parts, const nodes::NodeOptions& opt) {
    vb.hstack([&] {
        vb.widget(parts.label(opt.title, true));
        vb.spacer();
        if (!opt.family.empty()) vb.widget(parts.label(opt.family));
    });
}

void node_card(ViewBuilder& vb, NodeBoardParts& parts, const nodes::Graph& graph, nodes::NodeId id) {
    parts.card_ports.clear();
    const auto card = vb.card([&] {
        node_header(vb, parts, graph.options(id));
        if (const auto& view = graph.view(id)) view(vb);
    });
    for (auto& [port, place] : parts.card_ports) {
        place.card = card;
        parts.ports.insert_or_assign(port, place);
    }
}

// A port without a row has nowhere to draw its wires from.
[[maybe_unused]] bool every_port_placed(const nodes::Graph& graph, const NodeBoardParts& parts) {
    for (const auto id : parts.built)
        for (std::size_t i = 0; const auto* port = graph.port({id, i}); ++i)
            if (!parts.ports.contains(port)) return false;
    return true;
}

std::vector<core::Point> places(const nodes::Graph& graph, const NodeBoardParts& parts) {
    std::vector<core::Point> out;
    out.reserve(parts.built.size());
    for (const auto id : parts.built) out.push_back(graph.position(id));
    return out;
}

} // namespace

WidgetId ViewBuilder::node_board(nodes::Graph& graph) {
    placed_.insert(&graph.structure);
    placed_.insert(&graph.layout);
    auto parts = std::make_shared<NodeBoardParts>();
    Node b;
    b.id = tree_.next_id_++;
    b.is_leaf = false;
    b.layout_kind = LayoutKind::Board;
    b.on_change = on_either(graph.structure, graph.layout);
    // A new wire bumps `structure` too, but changes no card: only a change of nodes rebuilds.
    // simplify: a node added or removed rebuilds every card, so the other nodes' widgets get new
    // ids and lose their state (focus, a half-typed value). Fine for tens of nodes; past that, or
    // once that state matters, rebuild per node: one keyed child per NodeId, built only when new.
    b.content_stale = [&graph, parts] { return parts->built != graph.nodes(); };
    b.build_content = [&graph, parts, &tree = tree_](Node& target) {
        parts->built = graph.nodes();
        parts->ports.clear();
        parts->labels.clear();
        ViewBuilder vb{tree, target};
        vb.board_ = parts.get();
        vb.canvas(parts->background);
        for (const auto id : parts->built) node_card(vb, *parts, graph, id);
        assert(every_port_placed(graph, *parts) && "a node's view must place each of its ports with vb.port");
    };
    b.board_places = [&graph, parts] { return places(graph, *parts); };
    b.board_under = [&graph, parts, &theme = tree_.theme()](render::DrawList& dl, const BoardRectOf& rect_of) {
        draw_wires(dl, graph, *parts, rect_of, theme.accent);
    };
    b.board_over = [parts, &theme = tree_.theme()](render::DrawList& dl, const BoardRectOf& rect_of) {
        draw_dots(dl, *parts, rect_of, theme.accent);
    };
    current_parent().children.push_back(std::move(b));
    return current_parent().children.back().id;
}

ViewBuilder::LeafHandle ViewBuilder::port(nodes::Port& p) {
    // simplify: only the node's own builder knows the board; a port placed by a builder nested in
    // the view (a section's, a popup's) asserts here. Hand board_ on to nested builders if needed.
    assert(board_ && "vb.port places a port's row in a node's view, inside a node_board");
    const bool output = p.dir() == nodes::Dir::Out;
    const auto row = hstack([&] {
        if (output) spacer();
        widget(board_->label(std::string(p.name())));
        if (!output) spacer();
    });
    board_->card_ports.emplace_back(&p, PortPlace{.card = 0, .row = row, .dir = p.dir()});
    return {current_parent().children.back().children[output ? 1 : 0], placed_};
}

} // namespace prism::app
