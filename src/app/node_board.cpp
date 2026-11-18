#include "node_board_parts.hpp"

#include <prism/app/node_board.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/nodes/tidy.hpp>
#include <prism/nodes/wire_geometry.hpp>
#include <prism/ui/text_metrics.hpp>

#include <algorithm>
#include <any>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <memory>

namespace prism::app {

using namespace node_board_detail;

void BoardBackground::canvas(render::DrawList& dl, core::Rect r, const ui::WidgetNode& n) {
    dl.filled_rect(r, n.theme->node_board_bg);
    draw_grid(dl, r.extent, *n.theme, parts->graph->view());
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

} // namespace

// Every conversion between the graph's positions, the board's coordinates and the window's (a
// widget's own goes through ui::place_of): a zoom changes this one place.
namespace node_board_detail::board_coords {

// Where the graph's point `at` lies on the board at zoom 1 (the engine zooms the cards from there).
core::Point on_board(const nodes::Graph& graph, core::Point at) { return at + (graph.view().pan - core::Point{}); }

// Where a point of the graph dragged from `from` (a node's position, the pan) goes when the
// pointer has moved by `d` in widget `n`'s coordinates: to the window by n's zoom, then to the
// graph by the board's.
core::Point dragged(const nodes::Graph& graph, const ui::WidgetNode& n, core::Point from, core::Offset d) {
    const float k = n.view_scale / graph.view().zoom;
    return from + core::Offset{core::DX{d.dx.raw() * k}, core::DY{d.dy.raw() * k}};
}

// The graph point shown at `on_board`, a point in the board's own coordinates (a drop's place).
core::Point to_graph(const nodes::Graph& graph, core::Point on_board) {
    return nodes::graph_point(graph.view(), on_board - core::Point{});
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

} // namespace node_board_detail::board_coords

namespace {

// The wire within wire_reach of `p` (window coordinates), the nearest if several are.
// simplify: every wire is measured on every pointer move over the background, O(wires) each. A
// board with hundreds of wires wants a spatial index, or the test limited to the wires near `p`.
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


// A change of the probed output redraws the board: the bubble shows its value.
core::Connection watch_output(NodeBoardParts& parts, nodes::EdgeId edge) {
    return parts.graph->port(parts.graph->edge(edge)->from)->on_change([&parts] {
        if (shown_probe(parts)) parts.tree->mark_dirty_by_id(parts.background_id);
    });
}

void set_probe(NodeBoardParts& parts, std::optional<Probe> next) {
    const bool same_wire = parts.probe && next && parts.probe->edge == next->edge;
    if (!same_wire) {
        parts.probe_link.disconnect();
        if (next) parts.probe_link = watch_output(parts, next->edge);
    }
    parts.probe = next;
}

// The pointer rests on a wire, no button held: the probe is there. Anywhere else it is gone.
void probe_pointer(NodeBoardParts& parts, const ui::WidgetNode& n, const input::MouseMove& mm) {
    const auto at = board_coords::from_board_origin(parts, n, mm.position);
    const auto wire = at && mm.buttons == 0 ? wire_at(parts, ui::place_of(n).to_window(mm.position)) : std::nullopt;
    set_probe(parts, wire ? std::optional{Probe{*wire, *at}} : std::nullopt);
}

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
    if (const auto* mm = std::get_if<input::MouseMove>(&ev)) probe_pointer(*parts, n, *mm);
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

namespace {

void drop_guides(NodeBoardParts& parts) {
    if (parts.guides.empty()) return;
    parts.guides.clear();
    parts.tree->mark_dirty_by_id(parts.board_id);
}

// Alt held at the press: the node goes where the pointer puts it.
nodes::Snapped snapped_or_free(const NodeBoardParts& parts, nodes::NodeId id, core::Point to, std::uint16_t mods) {
    return (mods & input::mods::alt) ? nodes::Snapped{to, {}} : snapped_drag(parts, id, to);
}

} // namespace

// Its node may be gone already: the cards are rebuilt on the next frame.
void NodeGrip::handle_canvas_input(const input::InputEvent& ev, ui::WidgetNode& n, core::Rect) {
    nodes::Graph& graph = *parts->graph;
    WidgetTree& tree = *parts->tree;
    if (!std::ranges::contains(graph.nodes(), id)) return drop_guides(*parts);
    if (track_press(press, ev)) {
        mods = std::get<input::MouseButton>(ev).mods;
        shown = {{0, graph.position(id)}};
        set_selection(*parts, n, {.wire = {}, .node = id});
        if (parts->glide) parts->glide->legs.erase(id);   // grabbed: it stays where the hand puts it
        tree.set_focused(parts->background_id);   // the board takes Delete
    }
    if (!press) return drop_guides(*parts);
    const auto d = dragged_by(press, ev, n);
    if (!d) return;
    auto seen = shown.upper_bound(tree.input_frame());
    if (seen != shown.begin()) --seen;
    const core::Point pointer_at = board_coords::dragged(graph, n, seen->second, *d);
    const auto placed = snapped_or_free(*parts, id, pointer_at, mods);
    shown.erase(shown.begin(), seen);   // older frames are not shown any more
    parts->guides = placed.guides;
    graph.move(id, placed.at);
    shown.insert_or_assign(tree.next_frame_version(), placed.at);
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

// The title, then the grip stretching like a spacer. The family is the header's own chrome, drawn
// behind both by the frame: a label cannot take a colour, and the theme's is only known when drawn.
void node_header(ViewBuilder& vb, NodeBoardParts& parts, NodeGrip& grip, const nodes::NodeOptions& opt) {
    vb.framed(ui::LayoutKind::Row, {ui::FrameLook::NodeHeader, {}, opt.family}, [&] {
        grabs(vb.widget(parts.label(opt.title, true)).node_ref, grip);
        focus_by(vb.canvas(grip).node_ref, ui::FocusPolicy::none);
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
    const auto card = vb.framed(ui::LayoutKind::Column, {ui::FrameLook::NodeCard}, [&] {
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

// A zoom or a fit asked for is made once the board knows its size. A fit or a tidy keeps asking until two
// passes see the same board and card sizes: a wrapped label above the board settles on a later
// pass, and a fit made from an earlier pass's size would be off.
// simplify: a board with no size at all (zero-size) keeps the requests until a frame finds it
// sized; nothing asks for that frame, so a resize or an input brings it.
std::vector<core::Rect> cards_as_laid_out(const nodes::Graph& graph, const NodeBoardParts& parts) {
    std::vector<core::Rect> cards;
    for (const auto& [id, card] : parts.cards)
        if (const auto size = parts.tree->arranged_extent(card)) cards.push_back({graph.position(id), *size});
    return cards;
}

bool same_sizes_as_last_pass(NodeBoardParts& parts, core::Size board, const std::vector<core::Rect>& cards) {
    std::vector<core::Size> sizes;
    sizes.reserve(cards.size());
    for (const auto& card : cards) sizes.emplace_back(card.extent);
    const bool same = parts.seen_board == board && parts.seen_cards == sizes;
    parts.seen_board = board;
    parts.seen_cards = std::move(sizes);
    return same;
}

// Fits during this layout, which uses the view now; the sizes it read may be an earlier pass's,
// so until two passes agree the request stays and asks for the next pass.
void fit_as_asked(nodes::Graph& graph, const std::vector<core::Rect>& cards, core::Size board, bool stable) {
    graph.fit(cards, board);
    if (!stable) graph.request_fit();
}

// Zoom, tidy and fit as asked, once the board knows its size. A fit waits for a tidy's glide to
// end, so it shows the cards where they come to rest.
void view_as_asked(nodes::Graph& graph, NodeBoardParts& parts) {
    const auto board = parts.tree->laid_out_at(parts.board_id);
    if (!board || board->extent.w.raw() <= 0.f || board->extent.h.raw() <= 0.f) return;
    if (graph.zoom_requested()) graph.zoom_centred(board->extent);
    glide_on(graph, parts);
    const auto cards = cards_as_laid_out(graph, parts);
    const bool same = same_sizes_as_last_pass(parts, board->extent, cards);
    if (!graph.fit_requested() && !graph.tidy_requested()) return;
    const bool stable = same || ++parts.fit_waits >= fit_waits_limit;
    if (tidy_as_asked(graph, parts, cards.size(), stable)) glide_on(graph, parts);
    const bool gliding = parts.glide || graph.tidy_requested();
    if (graph.fit_requested() && !gliding) fit_as_asked(graph, cards, board->extent, stable);
    if (stable) parts.fit_waits = 0;
}

std::vector<core::Point> places(nodes::Graph& graph, NodeBoardParts& parts) {
    view_as_asked(graph, parts);
    std::vector<core::Point> out;
    out.reserve(parts.built.size());
    for (const auto id : parts.built) out.push_back(board_coords::on_board(graph, graph.position(id)));
    return out;
}

// A node the graph owns is freed when removed, before the board rebuilds: drop what the board
// keeps of it now, so no paint or hit test in between reaches its ports.
// simplify: O(ports x nodes) per removal (a linear lookup of each port's node); fine for tens of
// nodes. Keep a set of the live ids, built once per call, if boards grow past that.
void forget_removed_nodes(NodeBoardParts& parts) {
    const auto gone = [&](nodes::NodeId id) { return !std::ranges::contains(parts.graph->nodes(), id); };
    std::erase_if(parts.ports, [&](const auto& kv) { return gone(kv.second.ref.node); });
    std::erase_if(parts.cards, [&](const auto& kv) { return gone(kv.first); });
    if (parts.selected.node && gone(*parts.selected.node)) parts.selected.node.reset();
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
    parts->structure_link = graph.structure.on_change().connect([p = parts.get()](std::uint64_t) {
        forget_removed_nodes(*p);
        if (p->probe && !p->graph->edge(p->probe->edge)) set_probe(*p, std::nullopt);
    });
    parts->background.parts = parts.get();
    b.build_content = [&graph, parts, &tree = tree_](Node& target) {
        parts->built = graph.nodes();
        parts->ports.clear();
        parts->cards.clear();
        parts->labels.clear();
        parts->grips.clear();
        ViewBuilder vb{tree, target};
        vb.board_ = parts.get();
        // The grid is drawn from the view, and a canvas records its draws only when marked dirty:
        // every pan and zoom bumps `layout`, so depending on it redraws the grid where it belongs.
        ui::Node& background = vb.canvas(parts->background).depends_on(graph.layout).node_ref;
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
        draw_wires(dl, *parts, rect_of, scale, theme);
    };
    b.board_over = [parts, &theme = tree_.theme()](render::DrawList& dl, const BoardRectOf& rect_of, float scale) {
        draw_dots(dl, *parts, rect_of, scale, theme);
        outline_selected_node(dl, *parts, rect_of, scale, theme);
        draw_guides(dl, *parts, theme);
        draw_probe(dl, *parts, theme);
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
        if (output) spacer();          // the name sits by the dot, on the card's outward edge
        else gap(dot_clearance);
        widget(board_->label(std::string(p.name()))).node_ref.drag = port_source(*board_, ref);
        if (output) gap(dot_clearance);
        else spacer();
    });
    current_parent().children.back().drag = port_target(*board_->graph, ref);
    board_->card_ports.emplace_back(&p, PortPlace{.card = 0, .row = row, .dir = p.dir(), .ref = ref});
    constexpr std::size_t name_slot = 1;   // between the gap and the spacer, whichever side the dot is
    return {current_parent().children.back().children[name_slot], placed_};
}

} // namespace prism::app
