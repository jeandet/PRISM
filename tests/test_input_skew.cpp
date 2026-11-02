// Input is hit-tested against the frame the user saw when they clicked, not a newer one the
// logic thread published but the screen had not shown yet.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/headless_window.hpp>
#include <prism/app/model_app.hpp>

#include <chrono>
#include <condition_variable>
#include <atomic>
#include <mutex>
#include <thread>

using namespace prism;
using namespace prism::core;
using namespace prism::input;
using namespace prism::render;
using namespace prism::ui;
using namespace prism::app;
using namespace std::chrono_literals;

namespace {
// A button below a text area; growing the text area moves the button down.
struct Doc {
    Field<TextArea<>> notes{{.rows = 2}};
    Field<Button> go{{"Go"}};
    int clicks = 0;
    Connection c = go.on_change().connect([this](const Button&) { ++clicks; });
    void view(WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.widget(notes);
            vb.widget(go);
            vb.spacer();
        });
    }
};

// The rect of the frame's button (drawn with the text "Go").
std::optional<Rect> button_rect(const SceneSnapshot& snap) {
    for (std::size_t i = 0; i < snap.draw_lists.size(); ++i) {
        if (!snap.draw_lists[i]) continue;
        for (auto& c : snap.draw_lists[i]->commands)
            if (auto* t = std::get_if<TextCmd>(&c); t && t->text == "Go") return snap.geometry[i].second;
    }
    return std::nullopt;
}

// Shows the first frame, lets the app grow the text area (which moves the button down in the next
// frame), then clicks where the button was in the frame on screen -- stamped with that frame.
class StaleScreenBackend final : public BackendBase {
    HeadlessWindow window_{1, {.width = 400, .height = 300}};
    std::mutex m_;
    std::condition_variable cv_;
    std::vector<std::shared_ptr<const SceneSnapshot>> frames_;

public:
    std::atomic<AppContext*> ctx{nullptr};   // set by the app's setup
    Doc* model = nullptr;

    Window& create_window(WindowConfig) override { return window_; }
    void submit(WindowId, std::shared_ptr<const SceneSnapshot> s) override {
        { std::lock_guard l(m_); frames_.push_back(std::move(s)); }
        cv_.notify_all();
    }
    void wake() override {}
    void quit() override {}

    void run(std::function<void(const WindowEvent&)> send) override {
        std::unique_lock l(m_);
        cv_.wait_for(l, 2s, [&] { return !frames_.empty(); });
        REQUIRE_FALSE(frames_.empty());
        auto shown = frames_.front();
        auto button = button_rect(*shown);
        REQUIRE(button);
        Point at{button->origin.x + DX{5}, button->origin.y + DY{5}};
        std::size_t before = frames_.size();
        l.unlock();
        while (!ctx.load()) std::this_thread::yield();
        ctx.load()->post([m = model] {   // the text area grows: the button moves down
            auto ta = m->notes.get();
            ta.rows = 8;
            m->notes.set(ta);
        });
        l.lock();
        cv_.wait_for(l, 2s, [&] { return frames_.size() > before; });   // the newer frame exists
        REQUIRE(frames_.size() > before);
        auto moved = button_rect(*frames_.back());
        REQUIRE(moved);
        REQUIRE(moved->origin.y.raw() > at.y.raw());   // the newer frame really moved the button away
        l.unlock();
        send({1, MouseButton{at, buttons::left, true}, shown});
        send({1, MouseButton{at, buttons::left, false}, shown});
        send({1, WindowClose{}});
    }
};
}

TEST_CASE("a click lands on what was on screen, not on a newer unshown layout") {
    Doc model;
    auto impl = std::make_unique<StaleScreenBackend>();
    auto* backend_impl = impl.get();
    Backend backend{std::move(impl)};
    auto& window = backend.create_window({});
    backend_impl->model = &model;
    model_app(backend, window, model, [&](AppContext& ctx) { backend_impl->ctx = &ctx; });
    CHECK(model.clicks == 1);   // the button the user saw under the pointer
}

// --- recycled list rows ------------------------------------------------------------------

namespace {
struct Rows {
    core::List<std::string> rows;
    std::string clicked = "none";
    Rows() { for (auto r : {"r0", "r1", "r2", "r3", "r4"}) rows.push_back(r); }
    void view(WidgetTree::ViewBuilder& vb) {
        vb.list(rows, std::function<void(size_t, const std::string&)>{
            [this](size_t, const std::string& row) { clicked = row; }});
    }
};

std::optional<Rect> text_rect(const SceneSnapshot& snap, std::string_view text) {
    for (std::size_t i = 0; i < snap.draw_lists.size(); ++i) {
        if (!snap.draw_lists[i]) continue;
        for (auto& c : snap.draw_lists[i]->commands)
            if (auto* t = std::get_if<TextCmd>(&c); t && t->text == text) return snap.geometry[i].second;
    }
    return std::nullopt;
}

// Shows the first frame, lets the app change the list, then clicks r2 as shown on screen.
class StaleRowsBackend final : public BackendBase {
    HeadlessWindow window_{1, {.width = 400, .height = 300}};
    std::mutex m_;
    std::condition_variable cv_;
    std::vector<std::shared_ptr<const SceneSnapshot>> frames_;

public:
    std::atomic<AppContext*> ctx{nullptr};
    std::function<void(Rows&)> change;
    Rows* model = nullptr;

    Window& create_window(WindowConfig) override { return window_; }
    void submit(WindowId, std::shared_ptr<const SceneSnapshot> s) override {
        { std::lock_guard l(m_); frames_.push_back(std::move(s)); }
        cv_.notify_all();
    }
    void wake() override {}
    void quit() override {}
    void run(std::function<void(const WindowEvent&)> send) override {
        std::unique_lock l(m_);
        cv_.wait_for(l, 2s, [&] { return !frames_.empty(); });
        REQUIRE_FALSE(frames_.empty());
        auto shown = frames_.front();
        auto r2 = text_rect(*shown, "r2");
        REQUIRE(r2);
        Point at{r2->origin.x + DX{5}, r2->origin.y + DY{5}};
        std::size_t before = frames_.size();
        l.unlock();
        while (!ctx.load()) std::this_thread::yield();
        ctx.load()->post([this] { change(*model); });
        l.lock();
        cv_.wait_for(l, 2s, [&] { return frames_.size() > before; });
        l.unlock();
        send({1, MouseButton{at, buttons::left, true}, shown});
        send({1, MouseButton{at, buttons::left, false}, shown});
        send({1, WindowClose{}});
    }
};

std::string click_after(std::function<void(Rows&)> change) {
    Rows model;
    auto impl = std::make_unique<StaleRowsBackend>();
    auto* b = impl.get();
    b->model = &model;
    b->change = std::move(change);
    Backend backend{std::move(impl)};
    auto& window = backend.create_window({});
    model_app(backend, window, model, [&](AppContext& ctx) { b->ctx = &ctx; });
    return model.clicked;
}
}

TEST_CASE("a row click from a frame before the rows shifted is dropped, not given to a neighbour") {
    CHECK(click_after([](Rows& m) { m.rows.erase(0); }) == "none");   // r3 now sits where r2 was
}

TEST_CASE("a row click survives changes that move no row: an append") {
    CHECK(click_after([](Rows& m) { m.rows.push_back("r5"); }) == "r2");
}

// --- tables ----------------------------------------------------------------------------------

#include <prism/app/event_routing.hpp>

namespace {
struct Cols {
    std::vector<std::string> name = {"d", "a", "c", "b", "e"};
    size_t column_count() const { return 1; }
    size_t row_count() const { return name.size(); }
    std::string_view header(size_t) const { return "Name"; }
    std::string cell_text(size_t r, size_t) const { return name[r]; }
};
struct TableDoc {
    Cols data;
    void view(WidgetTree::ViewBuilder& vb) { vb.table(data); }
};

WidgetNode* find_table(WidgetNode& node) {
    if (node.layout_kind == LayoutKind::Table) return &node;
    for (auto& c : node.children)
        if (auto* t = find_table(c)) return t;
    return nullptr;
}
TableState& table_state(WidgetTree& tree) {
    auto* n = find_table(tree.root());
    REQUIRE(n);
    return **std::any_cast<std::shared_ptr<TableState>>(&n->edit_state);
}
}

TEST_CASE("a table row click from a frame before a re-sort moved the rows is dropped") {
    TableDoc doc;
    WidgetTree tree{doc};
    auto f1 = tree.build_snapshot(400, 300, 1);
    auto& ts = table_state(tree);
    float row_y = ts.row_height.raw() * 2.5f;   // the second body row
    using prism::app::widget_detail::route_event;
    route_event(tree, f1.get(), MouseButton{Point{X{20}, Y{5}}, buttons::left, true});   // header: sort
    route_event(tree, f1.get(), MouseButton{Point{X{20}, Y{5}}, buttons::left, false});
    REQUIRE(ts.sort.get());
    route_event(tree, f1.get(), MouseButton{Point{X{20}, Y{row_y}}, buttons::left, true});   // still frame 1
    route_event(tree, f1.get(), MouseButton{Point{X{20}, Y{row_y}}, buttons::left, false});
    CHECK(ts.selected_rows.get().empty());
    auto f2 = tree.build_snapshot(400, 300, 2);   // the sorted rows are on screen now
    route_event(tree, f2.get(), MouseButton{Point{X{20}, Y{row_y}}, buttons::left, true});
    CHECK(ts.selected_rows.get().size() == 1);
}
