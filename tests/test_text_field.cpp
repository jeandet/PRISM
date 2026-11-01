#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/delegate.hpp>
#include <prism/core/field.hpp>
#include <prism/app/widget_tree.hpp>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}


namespace {
prism::Theme test_theme;
prism::WidgetNode make_node(prism::WidgetVisualState vs = {}) {
    prism::WidgetNode node;
    node.visual_state = vs;
    node.theme = &test_theme;
    return node;
}
prism::Point P(float x, float y) { return {prism::X{x}, prism::Y{y}}; }
}

TEST_CASE("TextField default-constructs with empty value") {
    prism::TextField<> tf;
    CHECK(tf.value.empty());
    CHECK(tf.placeholder.empty());
    CHECK(tf.max_length == 0);
}

TEST_CASE("TextField equality comparison") {
    prism::TextField<> a{.value = "hello"};
    prism::TextField<> b{.value = "hello"};
    prism::TextField<> c{.value = "world"};
    CHECK(a == b);
    CHECK(a != c);
}

TEST_CASE("TextEditable concept matches TextField") {
    static_assert(prism::TextEditable<prism::TextField<>>);
    static_assert(!prism::TextEditable<std::string>);
    static_assert(!prism::TextEditable<int>);
}

TEST_CASE("char_width returns positive value") {
    CHECK(prism::char_width(14.f).raw() > 0.f);
    CHECK(prism::char_width(14.f).raw() == doctest::Approx(0.6f * 14.f));
}

TEST_CASE("Widget<TextField<>> has tab_and_click focus policy") {
    CHECK(prism::Widget<prism::TextField<>>::focus_policy == prism::FocusPolicy::tab_and_click);
}

// --- record() tests ---

TEST_CASE("TextField record produces background and text") {
    prism::Field<prism::TextField<>> field{{.value = "hello"}};
    prism::DrawList dl;
    auto node = make_node();
    prism::Widget<prism::TextField<>>::record(dl, field, node);

    CHECK(dl.size() >= 3);
    bool has_text = false;
    for (auto& cmd : dl.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "hello") has_text = true;
        }
    }
    CHECK(has_text);
}

TEST_CASE("TextField record shows placeholder when empty and unfocused") {
    prism::Field<prism::TextField<>> field{{.placeholder = "Enter name"}};
    prism::DrawList dl;
    auto node = make_node();
    prism::Widget<prism::TextField<>>::record(dl, field, node);

    bool has_placeholder = false;
    for (auto& cmd : dl.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "Enter name") has_placeholder = true;
        }
    }
    CHECK(has_placeholder);
}

TEST_CASE("TextField record hides placeholder when focused") {
    prism::Field<prism::TextField<>> field{{.placeholder = "Enter name"}};
    prism::DrawList dl;
    auto node = make_node({.focused = true});
    prism::Widget<prism::TextField<>>::record(dl, field, node);

    bool has_placeholder = false;
    for (auto& cmd : dl.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "Enter name") has_placeholder = true;
        }
    }
    CHECK_FALSE(has_placeholder);
}

TEST_CASE("TextField record shows cursor when focused") {
    prism::Field<prism::TextField<>> field{{.value = "hi"}};
    prism::DrawList dl;
    auto node = make_node({.focused = true});
    prism::Widget<prism::TextField<>>::record(dl, field, node);

    int thin_rects = 0;
    for (auto& cmd : dl.commands) {
        if (auto* fr = std::get_if<prism::FilledRect>(&cmd)) {
            if (fr->rect.extent.w.raw() <= 3.f) thin_rects++;
        }
    }
    CHECK(thin_rects >= 1);
}

TEST_CASE("TextField record uses clip_push/pop") {
    prism::Field<prism::TextField<>> field{{.value = "test"}};
    prism::DrawList dl;
    auto node = make_node();
    prism::Widget<prism::TextField<>>::record(dl, field, node);

    int clips = 0;
    for (auto& cmd : dl.commands) {
        if (std::holds_alternative<prism::ClipPush>(cmd)) clips++;
        if (std::holds_alternative<prism::ClipPop>(cmd)) clips++;
    }
    CHECK(clips == 2);
}

TEST_CASE("TextField record renders focus ring when focused") {
    prism::Field<prism::TextField<>> field{{.value = "test"}};
    prism::DrawList dl;
    auto node = make_node({.focused = true});
    prism::Widget<prism::TextField<>>::record(dl, field, node);

    bool has_outline = false;
    for (auto& cmd : dl.commands) {
        if (std::holds_alternative<prism::RectOutline>(cmd)) has_outline = true;
    }
    CHECK(has_outline);
}

TEST_CASE("TextField record sets Text cursor shape") {
    prism::Field<prism::TextField<>> field{{.value = "hello"}};
    prism::DrawList dl;
    auto node = make_node();
    prism::Widget<prism::TextField<>>::record(dl, field, node);

    CHECK(node.visual_state.cursor == prism::CursorShape::Text);
}

// --- handle_input() tests ---

TEST_CASE("TextField TextInput inserts text at cursor") {
    prism::Field<prism::TextField<>> field{{.value = "ab"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Widget<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 1;

    prism::Widget<prism::TextField<>>::handle_input(field, prism::TextInput{"X"}, node);
    CHECK(field.get().value == "aXb");
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 2);
}

TEST_CASE("TextField TextInput appends at end") {
    prism::Field<prism::TextField<>> field{{.value = "hi"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Widget<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 2;

    prism::Widget<prism::TextField<>>::handle_input(field, prism::TextInput{"!"}, node);
    CHECK(field.get().value == "hi!");
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 3);
}

TEST_CASE("TextField backspace deletes char before cursor") {
    prism::Field<prism::TextField<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Widget<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 2;

    prism::Widget<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::backspace, 0}, node);
    CHECK(field.get().value == "ac");
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 1);
}

TEST_CASE("TextField backspace at position 0 is no-op") {
    prism::Field<prism::TextField<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    prism::Widget<prism::TextField<>>::ensure_edit_state(node);

    prism::Widget<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::backspace, 0}, node);
    CHECK(field.get().value == "abc");
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 0);
}

TEST_CASE("TextField delete removes char at cursor") {
    prism::Field<prism::TextField<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Widget<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 1;

    prism::Widget<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::delete_, 0}, node);
    CHECK(field.get().value == "ac");
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 1);
}

TEST_CASE("TextField delete at end is no-op") {
    prism::Field<prism::TextField<>> field{{.value = "ab"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Widget<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 2;

    prism::Widget<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::delete_, 0}, node);
    CHECK(field.get().value == "ab");
}

TEST_CASE("TextField left arrow moves cursor left") {
    prism::Field<prism::TextField<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Widget<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 2;

    prism::Widget<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::left, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 1);
}

TEST_CASE("TextField left arrow at 0 stays at 0") {
    prism::Field<prism::TextField<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    prism::Widget<prism::TextField<>>::ensure_edit_state(node);

    prism::Widget<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::left, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 0);
}

TEST_CASE("TextField right arrow moves cursor right") {
    prism::Field<prism::TextField<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Widget<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 1;

    prism::Widget<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::right, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 2);
}

TEST_CASE("TextField right arrow at end stays at end") {
    prism::Field<prism::TextField<>> field{{.value = "ab"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Widget<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 2;

    prism::Widget<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::right, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 2);
}

TEST_CASE("TextField Home moves cursor to 0") {
    prism::Field<prism::TextField<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Widget<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 2;

    prism::Widget<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::home, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 0);
}

TEST_CASE("TextField End moves cursor to end") {
    prism::Field<prism::TextField<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    prism::Widget<prism::TextField<>>::ensure_edit_state(node);

    prism::Widget<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::end, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 3);
}

TEST_CASE("TextField max_length enforced on insert") {
    prism::Field<prism::TextField<>> field{{.value = "ab", .max_length = 3}};
    auto node = make_node({.focused = true});
    auto& es = prism::Widget<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 2;

    prism::Widget<prism::TextField<>>::handle_input(field, prism::TextInput{"XY"}, node);
    CHECK(field.get().value == "abX");
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 3);
}

TEST_CASE("TextField max_length blocks insert when full") {
    prism::Field<prism::TextField<>> field{{.value = "abc", .max_length = 3}};
    auto node = make_node({.focused = true});
    auto& es = prism::Widget<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 3;

    prism::Widget<prism::TextField<>>::handle_input(field, prism::TextInput{"X"}, node);
    CHECK(field.get().value == "abc");
}

// --- WidgetTree integration tests ---

struct TextFieldModel {
    prism::Field<prism::TextField<>> name{{.value = "hi"}};
    prism::Field<bool> flag{false};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(name, flag);
    }
};

TEST_CASE("TextField in WidgetTree creates focusable leaf") {
    TextFieldModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 2);

    auto focus = tree.focus_order();
    REQUIRE(focus.size() == 2);
}

TEST_CASE("TextField dispatch TextInput updates field value") {
    TextFieldModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();
    tree.set_focused(focus[0]);

    tree.dispatch(focus[0], prism::KeyPress{prism::keys::end, 0});
    tree.dispatch(focus[0], prism::TextInput{"X"});
    CHECK(model.name.get().value == "hiX");
}

TEST_CASE("TextField dispatch KeyPress backspace updates field value") {
    TextFieldModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();
    tree.set_focused(focus[0]);

    tree.dispatch(focus[0], prism::KeyPress{prism::keys::end, 0});
    tree.dispatch(focus[0], prism::KeyPress{prism::keys::backspace, 0});
    CHECK(model.name.get().value == "h");
}

// --- click/cursor tests ---

TEST_CASE("TextField click positions cursor") {
    prism::Field<prism::TextField<>> field{{.value = "abcdef"}};
    auto node = make_node({.focused = true});
    prism::Widget<prism::TextField<>>::ensure_edit_state(node);

    prism::Width cw = prism::char_width(14.f);
    float click_x = 4.f + 2.5f * cw.raw();
    prism::Widget<prism::TextField<>>::handle_input(
        field, prism::MouseButton{P(click_x, 15), 1, true}, node);
    auto cursor = std::any_cast<prism::TextEditState>(node.edit_state).cursor;
    CHECK((cursor == 2 || cursor == 3));
}

TEST_CASE("TextField scroll_offset adjusts when cursor moves past right edge") {
    prism::Field<prism::TextField<>> field{{.value = "abcdefghijklmnopqrstuvwxyz0123456789"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Widget<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 35;

    prism::Widget<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::end, 0}, node);

    prism::Width cw = prism::char_width(14.f);
    float text_area_w = 200.f - 2 * 4.f;
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).scroll_offset.raw() > 0.f);
    float cursor_px = 36 * cw.raw(); // end key moves cursor to len=36
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).scroll_offset.raw() ==
          doctest::Approx(cursor_px - text_area_w));
}

TEST_CASE("TextField scroll_offset resets when cursor moves to beginning") {
    prism::Field<prism::TextField<>> field{{.value = "abcdefghijklmnopqrstuvwxyz0123456789"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Widget<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 35;
    es.scroll_offset = prism::DX{100.f};

    prism::Widget<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::home, 0}, node);

    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).scroll_offset.raw() == 0.f);
}

TEST_CASE("TextField operations on empty string are safe") {
    prism::Field<prism::TextField<>> field{{.value = ""}};
    auto node = make_node({.focused = true});
    prism::Widget<prism::TextField<>>::ensure_edit_state(node);

    // All of these should be no-ops on empty string
    prism::Widget<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::backspace, 0}, node);
    CHECK(field.get().value.empty());

    prism::Widget<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::delete_, 0}, node);
    CHECK(field.get().value.empty());

    prism::Widget<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::left, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 0);

    prism::Widget<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::right, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 0);
}

TEST_CASE("TextField TextInput on empty field inserts text") {
    prism::Field<prism::TextField<>> field{{.value = ""}};
    auto node = make_node({.focused = true});
    prism::Widget<prism::TextField<>>::ensure_edit_state(node);

    prism::Widget<prism::TextField<>>::handle_input(field, prism::TextInput{"hello"}, node);
    CHECK(field.get().value == "hello");
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 5);
}

// --- UTF-8: the cursor is a byte offset that always sits on a code point boundary ---

TEST_CASE("TextField backspace removes a whole multi-byte character") {
    prism::Field<prism::TextField<>> field{{.value = "caf\xC3\xA9"}};   // "café", 5 bytes
    auto node = make_node({.focused = true});
    prism::Widget<prism::TextField<>>::ensure_edit_state(node).cursor = 5;
    prism::Widget<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::backspace, 0}, node);
    CHECK(field.get().value == "caf");
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 3);
}

TEST_CASE("TextField delete removes a whole multi-byte character") {
    prism::Field<prism::TextField<>> field{{.value = "\xC3\xA9" "a"}};
    auto node = make_node({.focused = true});
    prism::Widget<prism::TextField<>>::ensure_edit_state(node);
    prism::Widget<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::delete_, 0}, node);
    CHECK(field.get().value == "a");
}

TEST_CASE("TextField arrows step over whole characters") {
    prism::Field<prism::TextField<>> field{{.value = "a\xC3\xA9" "b"}};   // a é b: bytes 0 1 3 4
    auto node = make_node({.focused = true});
    auto& es = prism::Widget<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 4;
    auto key = [&](int32_t k) {
        prism::Widget<prism::TextField<>>::handle_input(field, prism::KeyPress{k, 0}, node);
        return std::any_cast<prism::TextEditState>(node.edit_state).cursor;
    };
    CHECK(key(prism::keys::left) == 3);
    CHECK(key(prism::keys::left) == 1);
    CHECK(key(prism::keys::right) == 3);
}

TEST_CASE("TextField max_length counts characters and never splits one") {
    prism::Field<prism::TextField<>> field{{.value = "", .max_length = 1}};
    auto node = make_node({.focused = true});
    prism::Widget<prism::TextField<>>::handle_input(
        field, prism::TextInput{"\xC3\xA9\xC3\xA9"}, node);
    CHECK(field.get().value == "\xC3\xA9");
}

TEST_CASE("TextField caret x follows the character column, not the byte offset") {
    prism::Field<prism::TextField<>> field{{.value = "\xC3\xA9\xC3\xA9"}};   // 2 chars, 4 bytes
    auto node = make_node({.focused = true});
    prism::Widget<prism::TextField<>>::ensure_edit_state(node).cursor = 4;
    prism::DrawList dl;
    prism::Widget<prism::TextField<>>::record(dl, field, node);
    float caret_x = -1.f;
    for (auto& cmd : dl.commands)
        if (auto* r = std::get_if<prism::FilledRect>(&cmd); r && r->rect.extent.w.raw() <= 3.f)
            caret_x = r->rect.origin.x.raw();
    float expected = 4.f + 2.f * prism::char_width(14.f).raw();   // padding + 2 columns
    CHECK(caret_x == doctest::Approx(expected));
}

// --- measured text (TextMeasurer in the theme) ---

namespace {
struct StubMeasurer final : prism::TextMeasurer {   // 'i' = 2, 'm' = 10, others 6
    float width(std::string_view s, float, prism::render::FontRole) const override {
        float w = 0;
        for (char c : s) w += c == 'i' ? 2.f : c == 'm' ? 10.f : 6.f;
        return w;
    }
};
}

TEST_CASE("TextField caret x uses the theme's text measurer") {
    StubMeasurer m;
    prism::Theme theme;
    theme.measurer = &m;
    prism::Field<prism::TextField<>> field{{.value = "mi"}};
    prism::WidgetNode node;
    node.theme = &theme;
    node.visual_state.focused = true;
    prism::Widget<prism::TextField<>>::ensure_edit_state(node).cursor = 2;
    prism::DrawList dl;
    prism::Widget<prism::TextField<>>::record(dl, field, node);
    float caret_x = -1.f;
    for (auto& cmd : dl.commands)
        if (auto* r = std::get_if<prism::FilledRect>(&cmd); r && r->rect.extent.w.raw() <= 3.f)
            caret_x = r->rect.origin.x.raw();
    CHECK(caret_x == doctest::Approx(4.f + 12.f));   // padding + width("mi")
}

TEST_CASE("TextField click maps x to the nearest measured character boundary") {
    StubMeasurer m;
    prism::Theme theme;
    theme.measurer = &m;
    prism::Field<prism::TextField<>> field{{.value = "mim"}};
    prism::WidgetNode node;
    node.theme = &theme;
    node.visual_state.focused = true;
    prism::Widget<prism::TextField<>>::handle_input(
        field, prism::MouseButton{P(4.f + 11.5f, 10.f), 1, true}, node);   // between "mi" (12) and "m" (10)
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 2);
}

// --- selection ---

namespace {
using TF = prism::Widget<prism::TextField<>>;
void key(prism::Field<prism::TextField<>>& f, prism::WidgetNode& n, int32_t k, uint16_t mods = 0) {
    TF::handle_input(f, prism::KeyPress{k, mods}, n);
}
const prism::TextEditState& state(const prism::WidgetNode& n) { return n.peek<prism::TextEditState>(); }
}

TEST_CASE("shift+arrows select, typing replaces the selection") {
    prism::Field<prism::TextField<>> field{{.value = "hello"}};
    auto node = make_node({.focused = true});
    TF::ensure_edit_state(node).cursor = 1;
    key(field, node, prism::keys::right, prism::mods::shift);
    key(field, node, prism::keys::right, prism::mods::shift);
    CHECK(state(node).anchor == std::optional<size_t>{1});
    CHECK(state(node).cursor == 3);
    TF::handle_input(field, prism::TextInput{"EY"}, node);
    CHECK(field.get().value == "hEYlo");
}

TEST_CASE("ctrl+A then backspace clears the field") {
    prism::Field<prism::TextField<>> field{{.value = "hello"}};
    auto node = make_node({.focused = true});
    key(field, node, prism::keys::a, prism::mods::ctrl);
    key(field, node, prism::keys::backspace);
    CHECK(field.get().value.empty());
}

TEST_CASE("left without shift collapses a selection to its start") {
    prism::Field<prism::TextField<>> field{{.value = "hello"}};
    auto node = make_node({.focused = true});
    auto& es = TF::ensure_edit_state(node);
    es.anchor = 1;
    es.cursor = 4;
    key(field, node, prism::keys::left);
    CHECK(state(node).cursor == 1);
    CHECK_FALSE(state(node).anchor.has_value());
}

TEST_CASE("the selection is highlighted from its start to its end") {
    prism::Field<prism::TextField<>> field{{.value = "hello"}};
    auto node = make_node({.focused = true});
    auto& es = TF::ensure_edit_state(node);
    es.anchor = 1;
    es.cursor = 3;
    prism::DrawList dl;
    TF::record(dl, field, node);
    float cw = prism::char_width(14.f).raw();
    bool found = false;
    for (auto& cmd : dl.commands)
        if (auto* r = std::get_if<prism::FilledRect>(&cmd); r && r->color == test_theme.text_selection) {
            CHECK(r->rect.origin.x.raw() == doctest::Approx(4.f + cw));
            CHECK(r->rect.extent.w.raw() == doctest::Approx(2.f * cw));
            found = true;
        }
    CHECK(found);
}

TEST_CASE("dragging the mouse selects") {
    prism::Field<prism::TextField<>> field{{.value = "hello"}};
    auto node = make_node({.focused = true});
    float cw = prism::char_width(14.f).raw();
    TF::handle_input(field, prism::MouseButton{P(4.f, 10.f), 1, true}, node);
    TF::handle_input(field, prism::MouseMove{P(4.f + 3.f * cw, 10.f)}, node);
    TF::handle_input(field, prism::MouseButton{P(4.f + 3.f * cw, 10.f), 1, false}, node);
    CHECK(state(node).anchor == std::optional<size_t>{0});
    CHECK(state(node).cursor == 3);
    TF::handle_input(field, prism::MouseMove{P(4.f + 5.f * cw, 10.f)}, node);   // released: no change
    CHECK(state(node).cursor == 3);
}

TEST_CASE("shift+click extends the selection") {
    prism::Field<prism::TextField<>> field{{.value = "hello"}};
    auto node = make_node({.focused = true});
    TF::ensure_edit_state(node).cursor = 1;
    float cw = prism::char_width(14.f).raw();
    TF::handle_input(field, prism::MouseButton{P(4.f + 4.f * cw, 10.f), 1, true, prism::mods::shift}, node);
    CHECK(state(node).anchor == std::optional<size_t>{1});
    CHECK(state(node).cursor == 4);
}

// --- clipboard ---

namespace {
struct RecordingClipboard final : prism::Clipboard {
    mutable std::vector<std::string> copied;
    void set_text(std::string utf8) const override { copied.push_back(std::move(utf8)); }
};
}

TEST_CASE("ctrl+C copies the selection; ctrl+X copies and removes it") {
    RecordingClipboard cb;
    prism::Theme theme;
    theme.clipboard = &cb;
    prism::Field<prism::TextField<>> field{{.value = "hello"}};
    prism::WidgetNode node;
    node.theme = &theme;
    auto& es = TF::ensure_edit_state(node);
    es.anchor = 1;
    es.cursor = 4;
    key(field, node, prism::keys::c, prism::mods::ctrl);
    REQUIRE(cb.copied.size() == 1);
    CHECK(cb.copied[0] == "ell");
    CHECK(field.get().value == "hello");
    key(field, node, prism::keys::x, prism::mods::gui);   // Cmd works too
    REQUIRE(cb.copied.size() == 2);
    CHECK(cb.copied[1] == "ell");
    CHECK(field.get().value == "ho");
}

TEST_CASE("copy without a selection does nothing") {
    RecordingClipboard cb;
    prism::Theme theme;
    theme.clipboard = &cb;
    prism::Field<prism::TextField<>> field{{.value = "hello"}};
    prism::WidgetNode node;
    node.theme = &theme;
    key(field, node, prism::keys::c, prism::mods::ctrl);
    CHECK(cb.copied.empty());
}

TEST_CASE("paste replaces the selection; a TextField flattens newlines to spaces") {
    prism::Field<prism::TextField<>> field{{.value = "hello"}};
    auto node = make_node({.focused = true});
    auto& es = TF::ensure_edit_state(node);
    es.anchor = 1;
    es.cursor = 4;
    TF::handle_input(field, prism::Paste{"a\r\nb\nc"}, node);
    CHECK(field.get().value == "ha b co");
}

// --- undo / redo ---

TEST_CASE("ctrl+Z undoes typing, ctrl+shift+Z and ctrl+Y redo") {
    prism::Field<prism::TextField<>> field{{.value = ""}};
    auto node = make_node({.focused = true});
    TF::handle_input(field, prism::TextInput{"a"}, node);
    TF::handle_input(field, prism::TextInput{"b"}, node);
    key(field, node, prism::keys::z, prism::mods::ctrl);
    CHECK(field.get().value.empty());
    key(field, node, prism::keys::z, prism::mods::ctrl | prism::mods::shift);
    CHECK(field.get().value == "ab");
    key(field, node, prism::keys::z, prism::mods::gui);   // Cmd+Z
    CHECK(field.get().value.empty());
    key(field, node, prism::keys::y, prism::mods::ctrl);
    CHECK(field.get().value == "ab");
}

TEST_CASE("a value changed by the program drops the undo history") {
    prism::Field<prism::TextField<>> field{{.value = ""}};
    auto node = make_node({.focused = true});
    TF::handle_input(field, prism::TextInput{"a"}, node);
    field.set({.value = "from the model"});
    key(field, node, prism::keys::z, prism::mods::ctrl);
    CHECK(field.get().value == "from the model");
}
