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
