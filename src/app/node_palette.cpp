#include <prism/app/node_board.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/ui/delegate.hpp>
#include <prism/ui/text_metrics.hpp>

#include <memory>
#include <vector>

namespace prism::app {

namespace {

constexpr float row_height = 26.f;
constexpr float row_width = 160.f;
constexpr float dot_radius = 5.f;
constexpr float dot_inset = 14.f;
constexpr float title_inset = 28.f;
constexpr float title_size = 14.f;
constexpr float cascade_step = 24.f;   // graph units between two nodes added in a row

struct Palette {
    nodes::Graph* graph = nullptr;
    std::vector<nodes::NodeKind> kinds;
    int added = 0;   // activations so far: each lands one step further down and right
};

// The graph point at the board's top-left, shifted by one step for each node added before.
core::Point next_spot(Palette& p) {
    const float shift = cascade_step * static_cast<float>(p.added++);
    return core::Point{} - (p.graph->view().pan - core::Point{}) + core::Offset{core::DX{shift}, core::DY{shift}};
}

void draw_row(render::DrawList& dl, core::Rect r, const ui::WidgetNode& n, const nodes::NodeKind& kind) {
    const ui::Theme& t = *n.theme;
    const auto& vs = ui::node_vs(n);
    const float mid = r.origin.y.raw() + r.extent.h.raw() / 2.f;
    if (vs.hovered) dl.filled_rect(r, t.surface_hover);
    dl.circle(core::Point{r.origin.x + core::DX{dot_inset}, core::Y{mid}}, dot_radius, ui::node_family_color(t, kind.family));
    const float lh = ui::line_height(t.measurer, title_size);
    dl.text(kind.title, core::Point{r.origin.x + core::DX{title_inset}, core::Y{mid - lh / 2.f}}, title_size, t.text);
    if (vs.focused) ui::delegate_detail::draw_focus_ring(dl, r.extent.w, r.extent.h, t);
}

bool activates(const input::InputEvent& ev) {
    if (const auto* mb = std::get_if<input::MouseButton>(&ev))
        return mb->pressed && mb->button == input::buttons::left && mb->clicks == 2;
    const auto* key = std::get_if<input::KeyPress>(&ev);
    return key && key->key == input::keys::enter;
}

void make_row(ui::WidgetNode& wn, const std::shared_ptr<Palette>& palette, std::size_t i) {
    wn.focus_policy = ui::FocusPolicy::tab_and_click;
    wn.record = [palette, i](ui::WidgetNode& node) {
        node.draws.clear();
        draw_row(node.draws, node.canvas_bounds, node, palette->kinds[i]);
    };
    wn.record(wn);
    wn.wire = [palette, i](ui::WidgetNode& node) {
        node.connections.push_back(node.on_input.connect([palette, i](const input::InputEvent& ev) {
            if (activates(ev)) palette->kinds[i].add(*palette->graph, next_spot(*palette));
        }));
    };
    wn.access = [palette, i] {
        ui::AccessInfo a;
        a.role = ui::Role::Button;
        a.name = palette->kinds[i].title;
        return a;
    };
}

ui::Node row_node(ui::WidgetId id, const std::shared_ptr<Palette>& palette, std::size_t i) {
    ui::Node n;
    n.id = id;
    n.is_leaf = true;
    n.layout_kind = ui::LayoutKind::Canvas;
    n.canvas_min_height = row_height;
    n.canvas_min_width = row_width;
    n.drag.source = [palette, i](const ui::WidgetNode&, core::Point) {
        return std::any{PaletteDrag{palette->graph, palette->kinds[i]}};
    };
    n.build_widget = [palette, i](ui::WidgetNode& wn) { make_row(wn, palette, i); };
    return n;
}

} // namespace

WidgetId ViewBuilder::node_palette(nodes::Graph& graph, std::span<const nodes::NodeKind> kinds) {
    const auto palette = std::make_shared<Palette>(&graph, std::vector<nodes::NodeKind>(kinds.begin(), kinds.end()));
    return vstack([&] {
        for (std::size_t i = 0; i < palette->kinds.size(); ++i)
            current_parent().children.push_back(row_node(tree_.next_id_++, palette, i));
    });
}

} // namespace prism::app
