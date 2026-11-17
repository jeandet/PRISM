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

namespace {

// The press point of a left button press, none on its release; untouched by anything else.
void track_press(std::optional<core::Point>& press, const input::InputEvent& ev) {
    if (auto* mb = std::get_if<input::MouseButton>(&ev); mb && mb->button == input::buttons::left)
        press = mb->pressed ? std::optional{mb->position} : std::nullopt;
}

// How far the pointer is from where it was pressed, while it drags.
std::optional<core::Offset> dragged_by(const std::optional<core::Point>& press, const input::InputEvent& ev,
                                       const ui::WidgetNode& n) {
    auto* mm = std::get_if<input::MouseMove>(&ev);
    if (!mm || !press || !ui::node_vs(n).pressed) return std::nullopt;
    return mm->position - *press;
}

} // namespace

// The background never moves, so the pan is simply where it was at the press plus the drag.
void BoardBackground::handle_canvas_input(const input::InputEvent& ev, ui::WidgetNode& n, core::Rect) {
    track_press(press, ev);
    if (std::holds_alternative<input::MouseButton>(ev)) pan_at_press = graph->pan;
    if (const auto d = dragged_by(press, ev, n)) {
        const core::Point to = pan_at_press + *d;
        graph->pan_by(to.x - graph->pan.x, to.y - graph->pan.y);
    }
}

void NodeGrip::handle_canvas_input(const input::InputEvent& ev, ui::WidgetNode& n, core::Rect) {
    track_press(press, ev);
    if (std::holds_alternative<input::MouseButton>(ev)) shown = {{0, graph->position(id)}};
    const auto d = dragged_by(press, ev, n);
    if (!d) return;
    auto seen = shown.upper_bound(tree->input_frame());
    if (seen != shown.begin()) --seen;
    const core::Point to = seen->second + *d;
    shown.erase(shown.begin(), seen);   // older frames are not shown any more
    graph->move(id, to);
    shown.insert_or_assign(tree->next_frame_version(), to);
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

// A canvas that only takes the mouse: Tab passes it by (node_canvas makes one taking input focusable).
void takes_no_focus(ui::Node& canvas) {
    canvas.build_widget = [inner = std::move(canvas.build_widget)](ui::WidgetNode& wn) {
        inner(wn);
        wn.focus_policy = ui::FocusPolicy::none;
    };
}

// A label that drags its node too: its input also goes to the grip.
void grabs(ui::Node& label, NodeGrip& grip) {
    label.build_widget = [inner = std::move(label.build_widget), &grip](ui::WidgetNode& wn) {
        inner(wn);
        wn.wire = [wire = std::move(wn.wire), &grip](ui::WidgetNode& node) {
            if (wire) wire(node);
            node.connections.push_back(node.on_input.connect([&grip, &node](const input::InputEvent& ev) {
                grip.handle_canvas_input(ev, node, {});
            }));
        };
    };
}

// The title, then the grip stretching like a spacer, then the family.
void node_header(ViewBuilder& vb, NodeBoardParts& parts, NodeGrip& grip, const nodes::NodeOptions& opt) {
    vb.hstack([&] {
        grabs(vb.widget(parts.label(opt.title, true)).node_ref, grip);
        takes_no_focus(vb.canvas(grip).node_ref);
        if (!opt.family.empty()) grabs(vb.widget(parts.label(opt.family)).node_ref, grip);
    });
}

// A node without a view (pure math, no controls): its inputs, then its outputs, in ports() order.
void default_ports(ViewBuilder& vb, const nodes::Graph& graph, nodes::NodeId id) {
    for (const auto dir : {nodes::Dir::In, nodes::Dir::Out})
        for (std::size_t i = 0; auto* port = graph.port({id, i}); ++i)
            if (port->dir() == dir) vb.port(*port);
}

void node_card(ViewBuilder& vb, NodeBoardParts& parts, nodes::Graph& graph, nodes::NodeId id, const WidgetTree& tree) {
    parts.card_ports.clear();
    auto& grip = parts.grips.emplace_back(NodeGrip{.graph = &graph, .id = id, .tree = &tree, .press = {}, .shown = {}});
    const auto card = vb.card([&] {
        node_header(vb, parts, grip, graph.options(id));
        if (const auto& view = graph.view(id)) view(vb);
        else default_ports(vb, graph, id);
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
    const core::Offset pan = graph.pan - core::Point{};
    for (const auto id : parts.built) out.push_back(graph.position(id) + pan);
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
        parts->grips.clear();
        parts->background.graph = &graph;
        ViewBuilder vb{tree, target};
        vb.board_ = parts.get();
        takes_no_focus(vb.canvas(parts->background).node_ref);
        for (const auto id : parts->built) node_card(vb, *parts, graph, id, tree);
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
