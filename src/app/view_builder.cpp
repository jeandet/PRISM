// ViewBuilder's bodies, out of the header so every translation unit that builds a view parses a
// declaration instead of ~900 lines of inline code. widget_tree.hpp includes view_builder.hpp at
// its own bottom (ViewBuilder needs WidgetTree complete), so the tree header goes first here.

#include <prism/app/widget_tree.hpp>
#include <prism/app/view_builder.hpp>

#include <prism/ui/text_metrics.hpp>

#include <algorithm>
#include <any>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace prism::app {

namespace view_builder_detail {

void show_selection(WidgetNode& wn, size_t index, Field<std::optional<std::size_t>>& selected,
                    WidgetTree& tree, WidgetId list_id) {
    wn.record = [record = std::move(wn.record), index, &selected](WidgetNode& node) {
        record(node);
        if (selected.get() != index) return;
        Rect row{Point{X{0}, Y{0}}, Size{delegate_detail::widget_w(node), node.draws.bounding_box().extent.h}};
        node.draws.filled_rect(row, node_theme(node).text_selection);
    };
    wn.record(wn);
    wn.wire = [wire = std::move(wn.wire), index, &selected, &tree, list_id](WidgetNode& node) {
        wire(node);
        node.connections.push_back(node.on_input.connect([index, &selected, &tree, list_id, &node](const InputEvent& ev) {
            auto* mb = std::get_if<MouseButton>(&ev);
            if (!mb || !mb->pressed || mb->button != buttons::left) return;
            if (node.focus_policy == FocusPolicy::none) tree.set_focused(list_id);
            selected.set(index);
        }));
        node.connections.push_back(selected.on_change().connect([&node](const auto&) { node.dirty = true; }));
    };
}

std::optional<std::size_t> list_key_target(std::optional<std::size_t> selected, std::size_t count,
                                           int32_t key) {
    if (count == 0) return std::nullopt;
    std::size_t last = count - 1;
    switch (key) {
    case keys::down: return selected ? std::min(*selected + 1, last) : 0;
    case keys::up: return selected ? std::min(*selected, count) - (*selected == 0 ? 0 : 1) : 0;
    case keys::home: return 0;
    case keys::end: return last;
    default: return std::nullopt;
    }
}

void unbind_vlist_row(WidgetNode& wn) {
    wn.connections.clear();
    wn.draws.clear();
    wn.overlay_draws.clear();
    wn.edit_state.reset();
    wn.wire = nullptr;
    wn.record = nullptr;
    wn.dirty = false;
}

DragRole tree_row_drag(TreeController& ctrl, std::size_t i) {
    if (!ctrl.on_move || i >= ctrl.rows.size()) return {};
    return {
        .source = [&ctrl, i](const WidgetNode&, Point) { return std::any(TreeRowDrag{&ctrl, i}); },
        .accepts = [&ctrl, i](const std::any& v) {
            auto* r = std::any_cast<TreeRowDrag>(&v);
            return r && r->tree == &ctrl && ctrl.can_move(r->row, i);
        },
        .drop = [&ctrl, i](const std::any& v, DropPlace at) {
            ctrl.move_row(std::any_cast<TreeRowDrag>(v).row, i,
                          TreeController::drop_zone(ctrl.rows[i], at.at.y.raw(), at.size.h.raw()));
        },
        .between = true,
        .inside_middle = ctrl.rows[i].has_children,
    };
}

void DialogParts::sync(Field<Dialog>& d, const TextMeasurer* m) {
    syncing = true;
    const Dialog& v = d.get();
    title.set({.value = v.title, .heading = true});
    auto msg = message.get();
    msg.value = v.message;
    msg.rows = std::clamp<std::size_t>(
        ui::wrap_lines(v.message, Width{message_wrap_w}, m, 14.f, FontRole::Ui).size(), 1, max_message_rows);
    message.set(msg);
    while (buttons.size() < v.buttons.size()) add_button(d);
    for (std::size_t i = 0; i < v.buttons.size(); ++i) {
        auto b = buttons[i]->get();
        b.text = v.buttons[i];
        b.primary = i == 0;   // the default one (Enter); the others are quieter
        buttons[i]->set(b);
    }
    syncing = false;
}

void DialogParts::add_button(Field<Dialog>& d) {
    std::size_t i = buttons.size();
    buttons.push_back(std::make_unique<Field<Button>>(Button{}));
    clicks.push_back(buttons.back()->on_change().connect([this, &d, i](const Button&) {
        if (syncing || !d.get().open) return;
        auto v = d.get();
        v.open = false;
        v.chosen = i;
        ++v.activations;
        d.set(v);
    }));
}

} // namespace view_builder_detail

void ViewBuilder::remember_split(Field<SplitSizes>& sizes) {
    placed_.insert(&sizes);
    auto& c = current_parent();
    c.split_get = [&sizes] { return sizes.get().value; };
    c.split_set = [&sizes](std::vector<float> v) { sizes.set({std::move(v)}); };
}

void ViewBuilder::section(Field<Expander>& state, std::function<void(ViewBuilder&)> build) {
    vstack([&] {
        widget(state);
        probe_placed(build);
        auto built_open = std::make_shared<std::optional<bool>>();
        Node c;
        c.id = tree_.next_id_++;
        c.is_leaf = false;
        c.layout_kind = LayoutKind::Column;
        c.on_change = view_builder_detail::adapt_on_change(state);   // a flip: the next frame checks
        c.content_stale = [&state, built_open] { return *built_open != state.get().open; };
        c.build_content = [&state, built_open, build, &tree = tree_](Node& target) {
            *built_open = state.get().open;
            if (!state.get().open) return;
            ViewBuilder vb{tree, target};
            build(vb);
        };
        current_parent().children.push_back(std::move(c));
    });
}

void ViewBuilder::add_spacer(float size) {
    Node s;
    s.id = tree_.next_id_++;
    s.is_leaf = true;
    s.layout_kind = LayoutKind::Spacer;
    s.spacer_size = size;
    current_parent().children.push_back(std::move(s));
}

void ViewBuilder::probe_placed(const std::function<void(ViewBuilder&)>& build) {
    Node scratch;
    ViewBuilder probe{tree_, scratch};
    build(probe);
    placed_.insert(probe.placed().begin(), probe.placed().end());
}

void ViewBuilder::draw_divider(WidgetNode& node) {
    node.draws.clear();
    auto& vs = node_vs(node);
    auto& t = node_theme(node);
    auto sz = node_allocated(node);
    auto color = (vs.pressed || vs.hovered) ? t.divider_hover : t.divider;
    node.draws.filled_rect(ui::delegate_detail::make_rect(X{0}, Y{0}, sz.w, sz.h), color);
}

void ViewBuilder::wire_split_handles(Node& container) {
    bool vertical = (container.layout_kind == LayoutKind::Column);
    WidgetId container_id = container.id;
    // pane_count tracks non-Handle children seen so far, matching how
    // SplitState::pane_sizes is built (one entry per non-Handle child, in
    // order) -- a handle's real "before" pane is pane_count - 1, which is
    // NOT the same as how many handles precede it whenever more than one
    // pane sits between two handles (or before the first one).
    size_t pane_count = 0;
    for (size_t i = 0; i < container.children.size(); ++i) {
        auto& child = container.children[i];
        if (child.layout_kind != LayoutKind::Handle) {
            ++pane_count;
            continue;
        }
        size_t index = pane_count - 1;

        bool has_left = (i > 0);
        bool has_right = (i + 1 < container.children.size());
        bool is_split_divider = has_left && has_right;

        child.build_widget = [&tree = tree_, container_id, index, vertical, is_split_divider](WidgetNode& wn) {
            wn.focus_policy = FocusPolicy::none;
            if (is_split_divider) {
                wn.visual_state.cursor = vertical ? CursorShape::ResizeNS : CursorShape::ResizeEW;
            }
            wn.record = &draw_divider;
            wn.wire = [&tree, container_id, index, vertical](WidgetNode& self) {
                self.connections.push_back(self.on_input.connect(
                    [&tree, container_id, index, vertical, &self](const InputEvent& ev) {
                        float origin = vertical ? self.absolute_y.raw() : self.absolute_x.raw();
                        if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->button == 1) {
                            float local = vertical ? mb->position.y.raw() : mb->position.x.raw();
                            if (mb->pressed) tree.begin_split_drag(container_id, index, local + origin);
                            else tree.end_split_drag();
                        } else if (auto* mm = std::get_if<MouseMove>(&ev); mm && node_vs(self).pressed) {
                            float local = vertical ? mm->position.y.raw() : mm->position.x.raw();
                            tree.update_split_drag(local + origin);
                        }
                    }));
            };
            wn.record(wn);
        };
    }
}

void ViewBuilder::handle() {
    Node h;
    h.id = tree_.next_id_++;
    h.is_leaf = true;
    h.layout_kind = LayoutKind::Handle;
    h.build_widget = [](WidgetNode& wn) {
        wn.focus_policy = FocusPolicy::none;
        wn.record = &draw_divider;
        wn.record(wn);
    };
    current_parent().children.push_back(std::move(h));
}

void ViewBuilder::tree(TreeController& ctrl, bool detail) {
    hstack([&] {
        Node container;
        container.id = tree_.next_id_++;
        container.is_leaf = false;
        container.layout_kind = LayoutKind::VirtualList;
        container.vlist_item_count = ctrl.rows.size();

        container.build_widget = [&ctrl, container_id = container.id, &tree = tree_](WidgetNode& wn) {
            wn.focus_policy = FocusPolicy::tab_and_click;
            wn.keeps_keys = [&ctrl](const KeyPress&) { return ctrl.renaming(); };
            wn.wire = [&ctrl, container_id, &tree](WidgetNode& self) {
                self.connections.push_back(
                    self.on_input.connect([&ctrl, container_id, &tree](const InputEvent& ev) {
                        if (auto* ti = std::get_if<TextInput>(&ev)) return ctrl.on_text(ti->text);
                        if (auto* p = std::get_if<Paste>(&ev)) return ctrl.on_text(text_detail::single_line(p->text));
                        auto idx = ctrl.on_key(ev);
                        if (idx)
                            tree.scroll_row_into_view(container_id, *idx, Widget<TreeRow>::row_h);
                    })
                );
            };
        };

        container.vlist_bind_row = [&ctrl, container_id = container.id, &tree = tree_](WidgetNode& wn, size_t index) {
            auto field_ptr = std::make_shared<Field<TreeRow>>(ctrl.rows[index]);
            wn.edit_state = std::shared_ptr<void>(field_ptr);
            wn.focus_policy = FocusPolicy::none; // rows never take focus; the container does
            wn.dirty = true;
            wn.is_container = false;
            wn.draws.clear();
            wn.overlay_draws.clear();
            wn.record = [field_ptr](WidgetNode& node) {
                node.draws.clear();
                node.overlay_draws.clear();
                Widget<TreeRow>::record(node.draws, *field_ptr, node);
            };
            wn.record(wn);
            wn.wire = [field_ptr, &ctrl, index, container_id, &tree](WidgetNode& node) {
                node.connections.push_back(
                    node.on_input.connect([&ctrl, index, container_id, &tree, &node](const InputEvent& ev) {
                        if (delegate_detail::is_activation_event(ev, node) && std::holds_alternative<MouseButton>(ev)
                            && index < ctrl.rows.size()) {
                            tree.set_focused(container_id);
                            ctrl.on_row_click(index, std::get<MouseButton>(ev));
                        }
                    })
                );
            };
        };

        container.vlist_unbind_row = &view_builder_detail::unbind_vlist_row;
        view_builder_detail::wire_list_signals(container, ctrl.rows);
        container.row_drag = [&ctrl](std::size_t i) { return view_builder_detail::tree_row_drag(ctrl, i); };

        current_parent().children.push_back(std::move(container));
        if (!detail) return;
        handle();
        widget(ctrl.detail);
    });
}

void ViewBuilder::finalize() {
    if (target_.children.size() > 1) {
        Node wrapper;
        wrapper.id = tree_.next_id_++;
        wrapper.is_leaf = false;
        wrapper.layout_kind = LayoutKind::Column;
        wrapper.children = std::move(target_.children);
        target_.children.clear();
        target_.children.push_back(std::move(wrapper));
    }
    // Hoist single Row/Column child: avoids an unnecessary nesting level
    // when view() produces exactly one container (the common case).
    if (target_.children.size() == 1 && !target_.children[0].build_content
        && !target_.children[0].frame) {   // a dock, a toolbar or a group stays itself
        auto lk = target_.children[0].layout_kind;
        if (lk == LayoutKind::Row || lk == LayoutKind::Column) {
            target_.layout_kind = lk;
            // Adopt the hoisted container's own id: callers that captured it
            // (e.g. hstack()'s single-lambda overload return value) must be
            // able to look it up in the built tree afterward -- without this,
            // the id returned to a view() like
            // `container_id = vb.hstack(...)` would refer to a Node discarded
            // right here, and begin_split_drag(container_id, ...) would
            // silently fail to find it.
            target_.id = target_.children[0].id;
            target_.split_get = std::move(target_.children[0].split_get);   // vb.remember_split
            target_.split_set = std::move(target_.children[0].split_set);
            // Detach first: move-assigning a vector from a member of its own element is UB
            // (libc++ frees the old buffer, which holds the source, before reading it).
            auto grandchildren = std::move(target_.children[0].children);
            target_.children = std::move(grandchildren);
        }
    }
}


namespace {
void publish_inner_scroll(WidgetNode& node, const text::View& view);
void highlight_in_idle_time(WidgetNode& node, text::View& view);

// A text view as a leaf: drawn like a canvas (it fills its space), its View kept in the node's
// edit state, where the tree's wheel, Page Up/Down and scrollbar find it (get_scroll_view).
Node text_view_node(text::Document& doc, std::shared_ptr<text::View> view, WidgetId& next_id) {
    Node n;
    n.id = next_id++;
    n.is_leaf = true;
    n.layout_kind = LayoutKind::Canvas;
    n.on_change = [&doc](std::function<void()> dirty) {
        return doc.on_change().connect([dirty = std::move(dirty)](const text::Change&) { dirty(); });
    };
    n.build_widget = [view](WidgetNode& wn) {
        wn.edit_state = view;
        wn.focus_policy = FocusPolicy::tab_and_click;
        // A sideways swipe scrolls it (up and down are the tree's); a press or drag in the minimap
        // scrolls to where it lands.
        if (!view->options().wrap || view->options().minimap)
            wn.wire = [view](WidgetNode& node) {
                auto held = std::make_shared<bool>(false);   // the minimap's band is held
                node.connections.push_back(node.on_input.connect([view, &node, held](const InputEvent& ev) {
                    if (auto* w = std::get_if<MouseScroll>(&ev); w && w->dx.raw() != 0.f && !view->options().wrap) {
                        view->scroll_sideways(-w->dx.raw() * 40.f);
                        node.dirty = true;
                    }
                    if (auto* b = std::get_if<MouseButton>(&ev); b && b->button == buttons::left) {
                        *held = b->pressed && view->in_minimap(b->position);
                        if (*held) { view->minimap_press(b->position); node.dirty = true; }
                    }
                    if (auto* m = std::get_if<MouseMove>(&ev); m && *held && (m->buttons & buttons::left_mask)) {
                        view->minimap_drag(m->position);
                        node.dirty = true;
                    }
                }));
            };
        wn.record = [view](WidgetNode& node) {
            static const Theme fallback{};
            node.draws.clear();
            view->draw(node.draws, node.canvas_bounds.extent, node.theme ? *node.theme : fallback);
            publish_inner_scroll(node, *view);
            highlight_in_idle_time(node, *view);
        };
        wn.record(wn);
    };
    return n;
}

// A code view's highlighting moves on past the screen a batch a frame (~3 ms), the tree's tick
// drawing it again until it is done.
void highlight_in_idle_time(WidgetNode& node, text::View& view) {
    constexpr std::size_t batch = 4000;
    if (view.highlight_ahead(batch)) node.redraw_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(16);
    else node.redraw_at.reset();
}

// The view's scrollbar: a leaf that scrolls its own content shows it like a scroll container's.
void publish_inner_scroll(WidgetNode& node, const text::View& view) {
    const float pad = view.options().padding;
    const Rect viewport{Point{X{pad}, Y{pad}}, Size{Width{node.canvas_bounds.extent.w.raw() - 2.f * pad}, view.viewport_h}};
    if (view.content_h.raw() > view.viewport_h.raw())
        node.inner_scroll = WidgetNode::InnerScroll{viewport, view.content_h, view.scroll};
    else
        node.inner_scroll.reset();
}

// An editor as a leaf: its view where the tree scrolls it (an aliasing pointer into the editor),
// a text-input role so typed text and editing shortcuts reach it, the caret shown when focused.
Node text_editor_node(text::Document& doc, std::shared_ptr<text::Editor> editor, WidgetId& next_id) {
    Node n;
    n.id = next_id++;
    n.is_leaf = true;
    n.layout_kind = LayoutKind::Canvas;
    n.on_change = [&doc](std::function<void()> dirty) {
        return doc.on_change().connect([dirty = std::move(dirty)](const text::Change&) { dirty(); });
    };
    n.build_widget = [editor](WidgetNode& wn) {
        wn.edit_state = std::shared_ptr<text::View>(editor, &editor->view);
        wn.focus_policy = FocusPolicy::tab_and_click;
        wn.visual_state.cursor = CursorShape::Text;
        wn.access = [editor] {
            AccessInfo a;
            a.role = Role::MultilineTextInput;
            return a;
        };
        if (editor->options().tab_inserts)
            wn.keeps_keys = [](const KeyPress& k) { return k.key == keys::tab; };
        wn.record = [editor](WidgetNode& node) {
            static const Theme fallback{};
            node.draws.clear();
            auto marks = editor->view.marks;
            if (!node.visual_state.focused) editor->view.marks.reset();   // no caret or selection unfocused
            editor->view.draw(node.draws, node.canvas_bounds.extent, node.theme ? *node.theme : fallback);
            editor->view.marks = marks;
            publish_inner_scroll(node, editor->view);
            highlight_in_idle_time(node, editor->view);
        };
        wn.record(wn);
        wn.wire = [editor](WidgetNode& node) {
            node.connections.push_back(node.on_input.connect([editor, &node](const InputEvent& ev) {
                if (editor->handle(ev, node.theme ? node.theme->clipboard : nullptr)) node.dirty = true;
            }));
        };
    };
    return n;
}

} // namespace

ViewBuilder::TextHandle ViewBuilder::code_editor(text::Document& doc, std::string_view language) {
    return text_editor(doc, {.view = {.family = render::FontRole::Mono, .wrap = false,
                                      .highlighter = language.empty() ? nullptr : &text::language(language),
                                      .line_numbers = true, .current_line = true, .minimap = true},
                             .tab_inserts = true, .auto_indent = true, .match_brackets = true});
}

ViewBuilder::TextHandle ViewBuilder::text_editor(text::Document& doc, text::EditorOptions options) {
    auto editor = std::make_shared<text::Editor>(doc, std::move(options));
    current_parent().children.push_back(text_editor_node(doc, editor, tree_.next_id_));
    return {current_parent().children.back(), placed_, std::shared_ptr<text::View>(editor, &editor->view)};
}

ViewBuilder::TextHandle ViewBuilder::text_view(text::Document& doc, text::ViewOptions options) {
    auto view = std::make_shared<text::View>(doc, options);
    current_parent().children.push_back(text_view_node(doc, view, tree_.next_id_));
    return {current_parent().children.back(), placed_, view};
}
} // namespace prism::app
