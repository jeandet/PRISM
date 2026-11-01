#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/text_edit.hpp>

using prism::ui::TextEdit;
namespace te = prism::ui::text_edit;

TEST_CASE("no selection when cursor and anchor meet") {
    TextEdit e{"abc", 1, 1};
    CHECK_FALSE(te::has_selection(e));
    CHECK(te::selected(e).empty());
}

TEST_CASE("the selection is the span between anchor and cursor, in either order") {
    CHECK(te::selected(TextEdit{"hello", 1, 4}) == "ell");
    CHECK(te::selected(TextEdit{"hello", 4, 1}) == "ell");
}

TEST_CASE("typing replaces the selection") {
    auto e = te::insert(TextEdit{"hello", 1, 4}, "EY", 0);
    CHECK(e.text == "hEYo");
    CHECK(e.cursor == 3);
    CHECK(e.anchor == 3);
}

TEST_CASE("backspace and delete remove the selection, or one character") {
    CHECK(te::erase_back(TextEdit{"hello", 1, 4}).text == "ho");
    CHECK(te::erase_forward(TextEdit{"hello", 4, 1}).text == "ho");
    auto e = te::erase_back(TextEdit{"a\xC3\xA9", 3, 3});   // "aé": one character, two bytes
    CHECK(e.text == "a");
    CHECK(e.cursor == 1);
    CHECK(te::erase_forward(TextEdit{"\xC3\xA9" "b", 0, 0}).text == "b");
    CHECK(te::erase_back(TextEdit{"ab", 0, 0}).text == "ab");      // nothing before the caret
    CHECK(te::erase_forward(TextEdit{"ab", 2, 2}).text == "ab");   // nothing after it
}

TEST_CASE("moving extends the selection or collapses it") {
    auto ext = te::move_to(TextEdit{"hello", 1, 1}, 4, true);
    CHECK(ext.anchor == 1);
    CHECK(ext.cursor == 4);
    auto col = te::move_to(ext, 2, false);
    CHECK(col.anchor == 2);
    CHECK(col.cursor == 2);
}

TEST_CASE("select_all selects the whole text, cursor at the end") {
    auto e = te::select_all(TextEdit{"hello", 2, 2});
    CHECK(te::selected(e) == "hello");
    CHECK(e.cursor == 5);
}

TEST_CASE("max_length counts characters left after the selection is replaced") {
    // 3 characters, limit 4, replacing 2 of them: room for 3 new characters
    auto e = te::insert(TextEdit{"abc", 0, 2}, "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 4);
    CHECK(e.text == "\xC3\xA9\xC3\xA9\xC3\xA9" "c");
}

TEST_CASE("offsets are snapped to character boundaries") {
    auto e = te::move_to(TextEdit{"a\xC3\xA9", 0, 0}, 2, false);   // byte 2 is inside "é"
    CHECK(e.cursor == 1);
}

// --- undo history ---

using prism::ui::EditHistory;

TEST_CASE("consecutive typing is one undo step; undo then redo restores both states") {
    EditHistory h;
    TextEdit e{"", 0, 0};
    for (const char* c : {"a", "b", "c"}) {
        TextEdit next = te::insert(e, c, 0);
        h.record(e, next, EditHistory::Kind::Typing);
        e = next;
    }
    auto undone = h.undo(e);
    REQUIRE(undone);
    CHECK(undone->text == "");
    auto redone = h.redo(*undone);
    REQUIRE(redone);
    CHECK(redone->text == "abc");
    CHECK_FALSE(h.redo(*redone));
}

TEST_CASE("a non-typing edit or a caret move starts a new step") {
    EditHistory h;
    TextEdit e{"", 0, 0};
    TextEdit a = te::insert(e, "a", 0);
    h.record(e, a, EditHistory::Kind::Typing);
    h.break_group();   // caret moved
    TextEdit b = te::insert(a, "b", 0);
    h.record(a, b, EditHistory::Kind::Typing);
    TextEdit c = te::erase_back(b);
    h.record(b, c, EditHistory::Kind::Other);
    CHECK(h.undo(c)->text == "ab");
    CHECK(h.undo(b)->text == "a");
    CHECK(h.undo(a)->text == "");
}

TEST_CASE("undo restores the selection that was replaced") {
    EditHistory h;
    TextEdit before{"hello", 4, 1};
    TextEdit after = te::insert(before, "X", 0);
    h.record(before, after, EditHistory::Kind::Other);
    auto back = h.undo(after);
    REQUIRE(back);
    CHECK(*back == before);
}

TEST_CASE("a new edit clears redo; the history is capped") {
    EditHistory h;
    TextEdit e{"", 0, 0};
    TextEdit a = te::insert(e, "a", 0);
    h.record(e, a, EditHistory::Kind::Other);
    h.undo(a);
    TextEdit b = te::insert(e, "b", 0);
    h.record(e, b, EditHistory::Kind::Other);
    CHECK_FALSE(h.redo(b));
    for (int i = 0; i < 500; ++i) {
        TextEdit n = te::insert(b, "x", 0);
        h.record(b, n, EditHistory::Kind::Other);
        b = n;
    }
    int steps = 0;
    while (auto u = h.undo(b)) { b = *u; ++steps; }
    CHECK(steps == static_cast<int>(EditHistory::capacity));
}

TEST_CASE("inserting into a full field keeps the selection instead of deleting it") {
    // "abcd" is over its limit of 3 (set by the program); with "a" selected, 3 characters are
    // kept, so there is no room: typing must not just delete the selection.
    auto e = te::insert(TextEdit{"abcd", 1, 0}, "x", 3);
    CHECK(e.text == "abcd");
    CHECK(e.anchor == 0);   // selection kept
    auto replace_all = te::insert(TextEdit{"abc", 3, 0}, "xyz", 2);
    CHECK(replace_all.text == "xy");
}
