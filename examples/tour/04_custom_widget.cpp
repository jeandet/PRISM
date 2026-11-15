// Tour 4: a value type with its own Widget<T>: record draws it, handle_input changes it.
#include <prism/prism.hpp>
#include <prism/app/run.hpp>
#include <algorithm>

using namespace prism;

struct Level { float value{0.5f}; bool operator==(const Level&) const = default; };   // 0 to 1, no GUI

namespace prism::ui {
template <>   // one specialization, and Level works anywhere a Field<T> does
struct Widget<Level> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    // The track is the widget's size and the accent is the level: a leaf is as big as what it draws.
    static void record(DrawList& dl, const Field<Level>& f, WidgetNode& node) {
        const Width w{std::max(200.f, node_allocated(node).w.raw())};
        dl.filled_rect({Point{X{0}, Y{0}}, Size{w, Height{8.f}}}, node_theme(node).track);
        dl.filled_rect({Point{X{0}, Y{0}}, Size{Width{w.raw() * f.get().value}, Height{8.f}}}, node_theme(node).accent);
    }
    static void handle_input(Field<Level>& f, const InputEvent& ev, WidgetNode& node) {
        const auto* kp = std::get_if<KeyPress>(&ev);
        if (!kp || (kp->key != keys::left && kp->key != keys::right)) return;
        auto level = f.get();
        level.value = std::clamp(level.value + (kp->key == keys::right ? 0.1f : -0.1f), 0.f, 1.f);
        f.set(level);
        node.dirty = true;
    }
};
} // namespace prism::ui

struct Meter {
    core::Field<Level> level{};
    void view(app::WidgetTree::ViewBuilder& vb) { vb.widget(level); }
};
int main() {
    Meter meter;
    return app::run(meter, {.title = "PRISM Tour 4: Your Own Widget", .width = 420, .height = 120});
}

// At scale: examples/widget_gallery/knob.hpp is a knob in one header; model_task_runner draws rows likewise.
