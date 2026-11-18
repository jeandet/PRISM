#include <prism/app/node_board.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/nodes/wire_geometry.hpp>

#include <algorithm>
#include <any>
#include <array>
#include <cassert>
#include <memory>

namespace prism::app {

void BoardBackground::canvas(render::DrawList& dl, core::Rect r, const ui::WidgetNode& n) {
    dl.filled_rect(r, n.theme->canvas_bg);
}

namespace {

// The press point of a `button` press, none on its release; untouched by anything else.
// True on a press: a drag starts there.
bool track_press(std::optional<core::Point>& press, const input::InputEvent& ev,
                 std::uint8_t button = input::buttons::left) {
    auto* mb = std::get_if<input::MouseButton>(&ev);
    if (!mb || mb->button != button) return false;
    press = mb->pressed ? std::optional{mb->position} : std::nullopt;
    return mb->pressed;
}

// How far the pointer is from where it was pressed, while it drags.
std::optional<core::Offset> dragged_by(const std::optional<core::Point>& press, const input::InputEvent& ev,
                                       const ui::WidgetNode& n) {
    auto* mm = std::get_if<input::MouseMove>(&ev);
    if (!mm || !press || !ui::node_vs(n).pressed) return std::nullopt;
    return mm->position - *press;
}

// Every conversion between the graph's positions, the board's coordinates and the window's (a
// widget's own goes through ui::place_of): a zoom changes this one place.
namespace board_coords {

// Where the graph's point `at` lies on the board at zoom 1 (the engine zooms the cards from there).
core::Point on_board(const nodes::Graph& graph, core::Point at) { return at + (graph.view().pan - core::Point{}); }

// Where a point of the graph dragged from `from` (a node's position, the pan) goes when the
// pointer has moved by `d` in widget `n`'s coordinates: to the window by n's zoom, then to the
// graph by the board's.
core::Point dragged(const nodes::Graph& graph, const ui::WidgetNode& n, core::Point from, core::Offset d) {
    const float k = n.view_scale / graph.view().zoom;
    return from + core::Offset{core::DX{d.dx.raw() * k}, core::DY{d.dy.raw() * k}};
}

// A point on the board (a drop's place) in the window; none before the board is laid out.
std::optional<core::Point> board_to_window(const NodeBoardParts& parts, core::Point on_board) {
    const auto board = parts.tree->laid_out_at(parts.board_id);
    return board ? std::optional{board->origin + (on_board - core::Point{})} : std::nullopt;
}


// Where `local`, a point in widget n's coordinates, is from the board's top-left, in the window.
std::optional<core::Offset> from_board_origin(const NodeBoardParts& parts, const ui::WidgetNode& n, core::Point local) {
    const auto board = parts.tree->laid_out_at(parts.board_id);
    return board ? std::optional{ui::place_of(n).to_window(local) - board->origin} : std::nullopt;
}

// Where the board's widgets lie, in the window.
ui::BoardRectOf window_rects(const NodeBoardParts& parts) {
    return [&tree = *parts.tree](ui::WidgetId id) { return tree.laid_out_at(id); };
}

} // namespace board_coords

// The wire within wire_reach of `p` (window coordinates), the nearest if several are.
std::optional<nodes::EdgeId> wire_at(const NodeBoardParts& parts, core::Point p) {
    constexpr float wire_reach = 6.f;
    const auto rect_of = board_coords::window_rects(parts);
    std::optional<nodes::EdgeId> best;
    float nearest = wire_reach;
    for (const auto id : parts.graph->edges()) {
        const auto* e = parts.graph->edge(id);
        const auto from = wire_end(parts, parts.graph->port(e->from), rect_of);
        const auto to = wire_end(parts, parts.graph->port(e->to), rect_of);
        if (!from || !to) continue;
        if (const float d = nodes::distance_to(nodes::wire_curve(*from, *to), p); d <= nearest) {
            nearest = d;
            best = id;
        }
    }
    return best;
}

// The port whose dot is within port_reach of `p` (window coordinates) and that `wanted` takes,
// the nearest if several are. Generous: a dot is small, and half of it lies off its card.
std::optional<nodes::PortRef> dot_near(const NodeBoardParts& parts, core::Point p,
                                       const std::function<bool(nodes::PortRef)>& wanted) {
    constexpr float port_reach = 10.f;
    const auto rect_of = board_coords::window_rects(parts);
    std::optional<nodes::PortRef> best;
    float nearest = port_reach;
    for (const auto& [port, place] : parts.ports) {
        const auto at = wire_end(parts, port, rect_of);
        if (!at || !wanted(place.ref)) continue;
        if (const float d = (*at - p).length(); d <= nearest) {
            nearest = d;
            best = place.ref;
        }
    }
    return best;
}

bool any_port(nodes::PortRef) { return true; }

// The selection is drawn by the board: `n`, a widget on it, asks for a frame.
void set_selection(NodeBoardParts& parts, ui::WidgetNode& n, BoardSelection what) {
    parts.selected = what;
    n.dirty = true;
}

bool removes_selection(const input::InputEvent& ev) {
    auto* kp = std::get_if<input::KeyPress>(&ev);
    return kp && (kp->key == input::keys::delete_ || kp->key == input::keys::backspace);
}

// A node that is not removable stays, and stays selected: its outline is still right.
void remove_selected(NodeBoardParts& parts) {
    nodes::Graph& graph = *parts.graph;
    const auto [wire, node] = parts.selected;
    const bool node_there = node && std::ranges::contains(graph.nodes(), *node);
    if (node_there && !graph.options(*node).removable) return;
    parts.selected = {};
    if (wire && graph.edge(*wire)) graph.disconnect(*wire);
    if (node_there) graph.remove(*node);
}

bool zooms(const input::InputEvent& ev) {
    auto* ms = std::get_if<input::MouseScroll>(&ev);
    return ms && (ms->mods & input::mods::ctrl);
}

// A Ctrl+wheel turn on the background: the board zooms, the point under the pointer staying there.
void zoom_by_wheel(NodeBoardParts& parts, const ui::WidgetNode& n, const input::MouseScroll& ms) {
    nodes::Graph& graph = *parts.graph;
    if (const auto pointer = board_coords::from_board_origin(parts, n, ms.position))
        graph.set_view(nodes::zoomed_about(graph.view(), *pointer, nodes::wheel_zoom(graph.view().zoom, ms.dy.raw())));
}

// Another button pressed or let go during a pan: the pan goes on.
bool other_button_mid_pan(const BoardBackground& bg, const ui::WidgetNode& n, const input::InputEvent& ev) {
    auto* mb = std::get_if<input::MouseButton>(&ev);
    return mb && bg.press && ui::node_vs(n).pressed && mb->button != bg.pan_button;
}

// The board claims every Ctrl+wheel turn and middle press on its cards for its background, which
// zooms and pans with them.
// simplify: desktop pinch gestures (SDL pinch events) are not read. In the browser a trackpad
// pinch arrives as a wheel event with ctrlKey set, which the web backend reports as Ctrl
// (sdl_platform_web.cpp), so it zooms. Read SDL_EVENT_PINCH_* when desktop users ask.
std::optional<ui::WidgetId> board_claims(const NodeBoardParts& parts, const input::InputEvent& ev) {
    auto* mb = std::get_if<input::MouseButton>(&ev);
    const bool claimed = zooms(ev) || (mb && mb->button == input::buttons::middle);
    return claimed && parts.background_id ? std::optional{parts.background_id} : std::nullopt;
}

} // namespace

// The background never moves, so the pan is simply where it was at the press plus the drag (in
// board units: a zoomed-out board pans further for the same drag). A middle drag only pans.
void BoardBackground::handle_canvas_input(const input::InputEvent& ev, ui::WidgetNode& n, core::Rect) {
    nodes::Graph& graph = *parts->graph;
    if (removes_selection(ev)) return remove_selected(*parts);
    if (zooms(ev)) return zoom_by_wheel(*parts, n, std::get<input::MouseScroll>(ev));
    if (other_button_mid_pan(*this, n, ev)) return;
    if (track_press(press, ev, input::buttons::middle)) {
        pan_button = input::buttons::middle;
        pan_at_press = graph.view().pan;
    } else if (track_press(press, ev)) {
        pan_button = input::buttons::left;
        const core::Point at = ui::place_of(n).to_window(*press);
        if (dot_near(*parts, at, any_port)) return press.reset();   // it draws a wire (dot_drag)
        pan_at_press = graph.view().pan;
        set_selection(*parts, n, {.wire = wire_at(*parts, at), .node = {}});
        if (parts->selected.wire) press.reset();   // a wire clicked is selected, not dragged
    }
    if (const auto d = dragged_by(press, ev, n)) {
        const core::Point to = board_coords::dragged(graph, n, pan_at_press, *d);
        graph.pan_by(to.x - graph.view().pan.x, to.y - graph.view().pan.y);
    }
}

// Its node may be gone already: the cards are rebuilt on the next frame.
void NodeGrip::handle_canvas_input(const input::InputEvent& ev, ui::WidgetNode& n, core::Rect) {
    nodes::Graph& graph = *parts->graph;
    WidgetTree& tree = *parts->tree;
    if (!std::ranges::contains(graph.nodes(), id)) return;
    if (track_press(press, ev)) {
        shown = {{0, graph.position(id)}};
        set_selection(*parts, n, {.wire = {}, .node = id});
        tree.set_focused(parts->background_id);   // the board takes Delete
    }
    const auto d = dragged_by(press, ev, n);
    if (!d) return;
    auto seen = shown.upper_bound(tree.input_frame());
    if (seen != shown.begin()) --seen;
    const core::Point to = board_coords::dragged(graph, n, seen->second, *d);
    shown.erase(shown.begin(), seen);   // older frames are not shown any more
    graph.move(id, to);
    shown.insert_or_assign(tree.next_frame_version(), to);
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
constexpr float selected_wire_thickness = wire_thickness + 2.f;
constexpr float dot_radius = 5.f;

// A size drawn to the cards' zoom, never below `floor` px: a wire or a dot zoomed far out stays seen.
float zoomed(float size, float scale, float floor) { return std::max(size * scale, floor); }

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

void draw_wires(render::DrawList& dl, const NodeBoardParts& parts, const ui::BoardRectOf& rect_of, float scale,
                Color color) {
    const nodes::Graph& graph = *parts.graph;
    for (const auto id : graph.edges()) {
        const auto* e = graph.edge(id);
        const auto from = wire_end(parts, graph.port(e->from), rect_of);
        const auto to = wire_end(parts, graph.port(e->to), rect_of);
        const float thickness = parts.selected.wire == id ? selected_wire_thickness : wire_thickness;
        if (from && to) dl.polyline(nodes::polyline(nodes::wire_curve(*from, *to)), color, zoomed(thickness, scale, 1.f));
    }
}

void draw_dots(render::DrawList& dl, const NodeBoardParts& parts, const ui::BoardRectOf& rect_of, float scale,
               Color color) {
    for (const auto& [port, place] : parts.ports)
        if (const auto at = wire_end(parts, port, rect_of)) dl.circle(*at, zoomed(dot_radius, scale, 3.f), color);
}

void outline_selected_node(render::DrawList& dl, const NodeBoardParts& parts, const ui::BoardRectOf& rect_of,
                           Color color) {
    const auto card = parts.selected.node ? parts.cards.find(*parts.selected.node) : parts.cards.end();
    if (card == parts.cards.end()) return;
    if (const auto r = rect_of(card->second)) dl.rect_outline(*r, color, 2.f);
}

// A wire being drawn: from the fixed end's port to the pointer (window coordinates).
void draw_trail(render::DrawList& dl, const NodeBoardParts& parts, nodes::PortRef from, core::Point pointer,
                Color color) {
    const nodes::Port* port = parts.graph->port(from);
    const auto end = wire_end(parts, port, board_coords::window_rects(parts));
    if (!end) return;
    const auto curve = port->dir() == nodes::Dir::Out ? nodes::wire_curve(*end, pointer) : nodes::wire_curve(pointer, *end);
    dl.polyline(nodes::polyline(curve), color, zoomed(wire_thickness, parts.graph->view().zoom, 1.f));
}

// What a drag from port `at` carries (a PortDrag). From an input with a wire, it picks the wire
// up: the wire goes now, and comes back only where the drag is dropped on a port taking it. A
// drop refused, or on nothing, leaves it gone, as node editors do. Nothing if the port's node
// is gone already (the cards are rebuilt on the next frame).
std::any pick_up(nodes::Graph& graph, nodes::PortRef at) {
    const nodes::Port* port = graph.port(at);
    if (!port) return {};
    if (port->dir() == nodes::Dir::In)
        if (const auto e = graph.edge_into(at)) {
            const nodes::PortRef from = graph.edge(*e)->from;
            graph.disconnect(*e);
            return PortDrag{&graph, from};
        }
    return PortDrag{&graph, at};
}

const PortDrag* port_drag_of(const nodes::Graph* graph, const std::any& v) {
    const auto* d = std::any_cast<PortDrag>(&v);
    return d && d->graph == graph ? d : nullptr;
}

// What every drag of a wire shares: the wire follows the pointer, and the ports taking it are
// hinted.
ui::DragRole wire_drag(NodeBoardParts& parts) {
    ui::DragRole role;
    role.trail = [&parts, &theme = parts.tree->theme()](render::DrawList& dl, const std::any& v, core::Point pointer) {
        draw_trail(dl, parts, std::any_cast<const PortDrag&>(v).from, pointer, theme.accent);
    };
    role.hint_targets = true;
    return role;
}

// A port's name: dragging it draws a wire from the port.
ui::DragRole port_source(NodeBoardParts& parts, nodes::PortRef at) {
    ui::DragRole role = wire_drag(parts);
    role.source = [graph = parts.graph, at](const ui::WidgetNode&, core::Point) { return pick_up(*graph, at); };
    return role;
}

// A port's row: a drop of a wire it can take, anywhere on it, connects it.
ui::DragRole port_target(nodes::Graph& graph, nodes::PortRef at) {
    ui::DragRole role;
    role.accepts = [&graph, at](const std::any& v) {
        const auto* d = port_drag_of(&graph, v);
        return d && graph.can_connect(d->from, at);
    };
    role.drop = [&graph, at](const std::any& v, ui::DropPlace) {
        static_cast<void>(graph.connect(std::any_cast<const PortDrag&>(v).from, at));
    };
    return role;
}

// The port whose row holds `p` (window coordinates), or else whose dot is near it.
std::optional<nodes::PortRef> port_under(const NodeBoardParts& parts, core::Point p) {
    for (const auto& [port, place] : parts.ports)
        if (const auto row = parts.tree->laid_out_at(place.row); row && row->contains(p)) return place.ref;
    return dot_near(parts, p, any_port);
}

// The background and the cards: a press on a port's row (beside its name) or near its dot
// draws a wire from it. Half of a dot lies off its card, on the background.
ui::DragRole dot_drag(NodeBoardParts& parts) {
    ui::DragRole role = wire_drag(parts);
    role.source = [&parts](const ui::WidgetNode& self, core::Point at) {
        const auto port = port_under(parts, ui::place_of(self).to_window(at));
        return port ? pick_up(*parts.graph, *port) : std::any{};
    };
    return role;
}

// The port whose dot near `p` (window coordinates) takes the drag `v`.
std::optional<nodes::PortRef> dot_taking(const NodeBoardParts& parts, const std::any& v, core::Point p) {
    const auto* d = port_drag_of(parts.graph, v);
    if (!d) return std::nullopt;
    return dot_near(parts, p, [&](nodes::PortRef to) { return parts.graph->can_connect(d->from, to); });
}

// The board: a drop near a dot connects to its port. It takes drops only there, so it is not
// hinted whole; the port taking one is marked by a bar on its card's edge (the drop's slot,
// an index into parts.ports).
// simplify: the slot, and so the bar, is the nearest dot, as DragRole::slot is not given the
// dragged value; the drop itself goes to the nearest dot taking it. They differ only when an
// incompatible dot is nearer, both within reach. Pass the value to slot() if that shows.
ui::DragRole dot_target(NodeBoardParts& parts) {
    constexpr float bar = 3.f;
    ui::DragRole role;
    role.accepts = [&parts](const std::any& v) { return port_drag_of(parts.graph, v) != nullptr; };
    role.takes_at = [&parts](const std::any& v, ui::DropPlace place) {
        const auto p = board_coords::board_to_window(parts, place.at);
        return p && dot_taking(parts, v, *p);
    };
    role.slot = [&parts](const ui::WidgetNode& self, core::Point at) {
        const auto port = dot_near(parts, ui::place_of(self).to_window(at), any_port);
        const auto it = std::ranges::find_if(parts.ports, [&](const auto& kv) { return port && kv.second.ref == *port; });
        return static_cast<std::size_t>(std::distance(parts.ports.begin(), it));
    };
    role.slot_marker = [&parts](const ui::WidgetNode& self, std::size_t slot) {
        if (slot >= parts.ports.size()) return core::Rect{};
        const auto& [port, place] = *std::next(parts.ports.begin(), static_cast<std::ptrdiff_t>(slot));
        const auto row = parts.tree->laid_out_at(place.row);
        const auto end = wire_end(parts, port, board_coords::window_rects(parts));
        if (!row || !end) return core::Rect{};
        return core::Rect{ui::place_of(self).to_local(core::Point{core::X{end->x.raw() - bar / 2}, row->origin.y}),
                          core::Size{core::Width{bar}, row->extent.h}};
    };
    role.drop = [&parts](const std::any& v, ui::DropPlace place) {
        const auto p = board_coords::board_to_window(parts, place.at);
        if (const auto to = p ? dot_taking(parts, v, *p) : std::nullopt)
            static_cast<void>(parts.graph->connect(std::any_cast<const PortDrag&>(v).from, *to));
    };
    return role;
}

nodes::PortRef ref_of(const NodeBoardParts& parts, const nodes::Port& p) {
    std::size_t i = 0;
    for (; const auto* q = parts.graph->port({parts.card_node, i}); ++i)
        if (q == &p) break;
    assert(parts.graph->port({parts.card_node, i}) && "vb.port places a port of the node whose card it is in");
    return {parts.card_node, i};
}

// A canvas taking the mouse, focused as `policy` says (node_canvas makes it focusable by Tab).
void focus_by(ui::Node& canvas, ui::FocusPolicy policy) {
    canvas.build_widget = [inner = std::move(canvas.build_widget), policy](ui::WidgetNode& wn) {
        inner(wn);
        wn.focus_policy = policy;
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
        focus_by(vb.canvas(grip).node_ref, ui::FocusPolicy::none);
        if (!opt.family.empty()) grabs(vb.widget(parts.label(opt.family)).node_ref, grip);
    });
}

// A node without a view (pure math, no controls): its inputs, then its outputs, in ports() order.
void default_ports(ViewBuilder& vb, const nodes::Graph& graph, nodes::NodeId id) {
    for (const auto dir : {nodes::Dir::In, nodes::Dir::Out})
        for (std::size_t i = 0; auto* port = graph.port({id, i}); ++i)
            if (port->dir() == dir) vb.port(*port);
}

// A card on `board`, the node `id`'s.
void node_card(ViewBuilder& vb, ui::Node& board, NodeBoardParts& parts, nodes::NodeId id) {
    const nodes::Graph& graph = *parts.graph;
    parts.card_ports.clear();
    parts.card_node = id;
    auto& grip = parts.grips.emplace_back(NodeGrip{.parts = &parts, .id = id, .press = {}, .shown = {}});
    const auto card = vb.card([&] {
        node_header(vb, parts, grip, graph.options(id));
        if (const auto& view = graph.view(id)) view(vb);
        else default_ports(vb, graph, id);
    });
    board.children.back().drag = dot_drag(parts);
    parts.cards.insert_or_assign(id, card);
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

// A fit asked for is made once the board knows its size: not at its first layout pass. The
// first publish lays out again once the cards' labels are measured, so a fit asked at startup
// lands there.
// simplify: a board with no size at all (zero-size) keeps the request until a frame finds it
// sized; nothing asks for that frame, so a resize or an input brings it.
void fit_if_asked(nodes::Graph& graph, const NodeBoardParts& parts) {
    const auto board = parts.tree->laid_out_at(parts.board_id);
    if (!graph.fit_requested() || !board || board->extent.w.raw() <= 0.f || board->extent.h.raw() <= 0.f) return;
    std::vector<core::Rect> cards;
    for (const auto& [id, card] : parts.cards)
        if (const auto size = parts.tree->arranged_extent(card)) cards.push_back({graph.position(id), *size});
    // Sets the view during this layout, which uses it now. The `layout` bump costs one more
    // publish (WidgetTree::clear_dirty keeps it), which is what shows the new view elsewhere.
    graph.fit(cards, board->extent);
}

std::vector<core::Point> places(nodes::Graph& graph, const NodeBoardParts& parts) {
    fit_if_asked(graph, parts);
    std::vector<core::Point> out;
    out.reserve(parts.built.size());
    for (const auto id : parts.built) out.push_back(board_coords::on_board(graph, graph.position(id)));
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
    b.drag = dot_target(*parts);
    b.on_change = on_either(graph.structure, graph.layout);
    // A new wire bumps `structure` too, but changes no card: only a change of nodes rebuilds.
    // simplify: a node added or removed rebuilds every card, so the other nodes' widgets get new
    // ids and lose their state (focus, a half-typed value). Fine for tens of nodes; past that, or
    // once that state matters, rebuild per node: one keyed child per NodeId, built only when new.
    b.content_stale = [&graph, parts] { return parts->built != graph.nodes(); };
    parts->graph = &graph;
    parts->tree = &tree_;
    parts->board_id = b.id;
    parts->background.parts = parts.get();
    b.build_content = [&graph, parts, &tree = tree_](Node& target) {
        parts->built = graph.nodes();
        parts->ports.clear();
        parts->cards.clear();
        parts->labels.clear();
        parts->grips.clear();
        ViewBuilder vb{tree, target};
        vb.board_ = parts.get();
        ui::Node& background = vb.canvas(parts->background).node_ref;
        parts->background_id = background.id;
        focus_by(background, ui::FocusPolicy::click);
        background.drag = dot_drag(*parts);
        for (const auto id : parts->built) node_card(vb, target, *parts, id);
        assert(every_port_placed(graph, *parts) && "a node's view must place each of its ports with vb.port");
    };
    b.board_places = [&graph, parts] { return places(graph, *parts); };
    b.board_scale = [&graph] { return graph.view().zoom; };
    b.claims = [parts](const input::InputEvent& ev) { return board_claims(*parts, ev); };
    b.board_under = [parts, &theme = tree_.theme()](render::DrawList& dl, const BoardRectOf& rect_of, float scale) {
        draw_wires(dl, *parts, rect_of, scale, theme.accent);
    };
    b.board_over = [parts, &theme = tree_.theme()](render::DrawList& dl, const BoardRectOf& rect_of, float scale) {
        draw_dots(dl, *parts, rect_of, scale, theme.accent);
        outline_selected_node(dl, *parts, rect_of, theme.accent);
    };
    current_parent().children.push_back(std::move(b));
    return current_parent().children.back().id;
}

ViewBuilder::LeafHandle ViewBuilder::port(nodes::Port& p) {
    // simplify: only the node's own builder knows the board; a port placed by a builder nested in
    // the view (a section's, a popup's) asserts here. Hand board_ on to nested builders if needed.
    assert(board_ && "vb.port places a port's row in a node's view, inside a node_board");
    const bool output = p.dir() == nodes::Dir::Out;
    const nodes::PortRef ref = ref_of(*board_, p);
    const auto row = hstack([&] {
        if (output) spacer();
        widget(board_->label(std::string(p.name()))).node_ref.drag = port_source(*board_, ref);
        if (!output) spacer();
    });
    current_parent().children.back().drag = port_target(*board_->graph, ref);
    board_->card_ports.emplace_back(&p, PortPlace{.card = 0, .row = row, .dir = p.dir(), .ref = ref});
    return {current_parent().children.back().children[output ? 1 : 0], placed_};
}

} // namespace prism::app
