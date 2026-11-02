// Pixel tests: real widgets rendered through the real SDL backend (offscreen driver), at 1x and
// 2x. They check properties of the pixels -- a selection covers exactly its word's ink, an icon
// doesn't overlap its label -- rather than comparing to reference images, so they hold across
// FreeType versions and font hinting while still catching the bugs that only real pixels show.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/widget_tree.hpp>
#include <prism/backends/sdl_font_set.hpp>
#include <prism/backends/sdl_text_measurer.hpp>
#include <prism/backends/sdl_window.hpp>
#ifdef PRISM_TEST_WEBGPU
#include <prism/backends/wgpu_renderer.hpp>
#endif

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app;
}
using namespace prism;

namespace {

struct Frame {
    int w = 0, h = 0;
    std::vector<std::uint8_t> rgba;
    const std::uint8_t* at(int x, int y) const { return &rgba[(static_cast<size_t>(y) * w + x) * 4]; }
    bool differs(const Frame& o, int x, int y, int threshold = 24) const {
        auto *a = at(x, y), *b = o.at(x, y);
        return std::abs(a[0] - b[0]) + std::abs(a[1] - b[1]) + std::abs(a[2] - b[2]) > threshold;
    }
};

// Column ranges [first, last] of x where `ink(x)` holds, merged across gaps below `min_gap`.
template <typename Ink>
std::vector<std::pair<int, int>> runs(int w, int min_gap, Ink ink) {
    std::vector<std::pair<int, int>> out;
    for (int x = 0; x < w; ++x) {
        if (!ink(x)) continue;
        if (!out.empty() && x - out.back().second <= min_gap) out.back().second = x;
        else out.push_back({x, x});
    }
    return out;
}

enum class Via { Sdl, WebGpu };

// One font set, rendering a model's widget tree at a device scale: through an SDL window
// (SDL_Renderer), or offscreen through the WebGPU renderer with the same fonts.
struct Rig {
    static constexpr int w = 480, h = 60;
    explicit Rig(Via via = Via::Sdl) : via(via) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));
        REQUIRE(TTF_Init());
        REQUIRE(fonts.open(PRISM_TEST_UI_FONT_PATH, PRISM_TEST_MONO_FONT_PATH, 14.f));
    }
    ~Rig() {
        fonts.close();
        TTF_Quit();
        SDL_Quit();
    }

    template <typename Model>
    Frame render(WidgetTree& tree, float scale, const char* name) {
#ifdef PRISM_TEST_WEBGPU
        if (via == Via::WebGpu) return render_webgpu(tree, scale, name);
#endif
        backends::SdlWindow win{1, WindowConfig{.title = name, .width = static_cast<int>(w * scale),
                                                .height = static_cast<int>(h * scale),
                                                .decoration = DecorationMode::None}};
        win.ensure_created();
        win.force_device_scale(scale);
        tree.set_text_measurer(&measurer);
        auto snap = tree.build_snapshot(w, h, ++version);
        win.render_snapshot(*snap, fonts, tree.theme(), /*present=*/false);
        SDL_Surface* raw = SDL_RenderReadPixels(win.renderer(), nullptr);
        REQUIRE(raw);
        SDL_Surface* px = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(raw);
        REQUIRE(px);
        if (const char* dir = std::getenv("PRISM_PIXEL_DUMP"))   // for looking at a failure
            SDL_SaveBMP(px, (std::string(dir) + "/" + name + ".bmp").c_str());
        Frame f{px->w, px->h, {}};
        for (int y = 0; y < px->h; ++y) {
            auto* row = static_cast<std::uint8_t*>(px->pixels) + static_cast<size_t>(y) * px->pitch;
            f.rgba.insert(f.rgba.end(), row, row + px->w * 4);
        }
        SDL_DestroySurface(px);
        return f;
    }

#ifdef PRISM_TEST_WEBGPU
    Frame render_webgpu(WidgetTree& tree, float scale, const char* name) {
        static auto ctx = backends::WgpuContext::create();
        REQUIRE(ctx);
        tree.set_text_measurer(&measurer);
        auto snap = tree.build_snapshot(w, h, ++version);
        int pw = static_cast<int>(w * scale), ph = static_cast<int>(h * scale);
        auto* f = &fonts;
        Frame out{pw, ph, backends::render_to_pixels(*ctx, *snap, pw, ph, scale, tree.theme().canvas_bg,
                    [f](std::string_view text, float px, FontRole role) { return f->rasterize(text, px, role); })};
        REQUIRE(out.rgba.size() == static_cast<size_t>(pw) * ph * 4);
        if (const char* dir = std::getenv("PRISM_PIXEL_DUMP"))
            if (SDL_Surface* s = SDL_CreateSurfaceFrom(pw, ph, SDL_PIXELFORMAT_RGBA32, out.rgba.data(), pw * 4)) {
                SDL_SaveBMP(s, (std::string(dir) + "/webgpu-" + name + ".bmp").c_str());
                SDL_DestroySurface(s);
            }
        return out;
    }
#endif

    Via via;
    backends::SdlFontSet fonts;
    backends::SdlTextMeasurer measurer{PRISM_TEST_UI_FONT_PATH, PRISM_TEST_MONO_FONT_PATH};
    std::uint64_t version = 0;
};

// Frame pixels per logical pixel: the forced scale, times the display's on a HiDPI desktop.
int device_scale(const Frame& f) { return std::max(1, f.w / Rig::w); }

WidgetNode& first_leaf(WidgetTree& tree) {
    WidgetNode* n = &tree.root();
    while (n->is_container && !n->children.empty()) n = &n->children.front();
    return *n;
}

struct Field1 {
    Field<TextField<>> text;
    void view(WidgetTree::ViewBuilder& vb) { vb.widget(text); }
};

constexpr int ink_threshold = 60;

// Ink: pixels far from the field's background colour, in the text's half of the field.
struct Ink {
    const Frame& f;
    const std::uint8_t* bg;
    explicit Ink(const Frame& frame) : f(frame), bg(frame.at(frame.w - 2, frame.h / 4)) {}
    bool at(int x, int y) const {
        auto* p = f.at(x, y);
        return std::abs(p[0] - bg[0]) + std::abs(p[1] - bg[1]) + std::abs(p[2] - bg[2]) > ink_threshold;
    }
    bool column(int x, int top, int bottom) const {
        for (int y = top; y <= bottom; ++y) if (at(x, y)) return true;
        return false;
    }
};

// The value drawn in a text field: no selection unless `selection` is given.
Frame field_frame(Rig& rig, std::string value, float scale, std::optional<std::pair<size_t, size_t>> selection = {}) {
    Field1 model;
    model.text.set({.value = std::move(value)});
    WidgetTree tree{model};
    if (selection) {
        auto& es = first_leaf(tree).get_or_create<TextEditState>();
        es.anchor = selection->first;
        es.cursor = selection->second;
    }
    std::string tag = "field@" + std::to_string(static_cast<int>(scale)) + "x-" + std::to_string(rig.version);
    return rig.render<Field1>(tree, scale, tag.c_str());
}

void check_selection_covers_its_word(float scale, Via via = Via::Sdl) {
    Rig rig{via};
    // The selected word sits far along the line: measurement drift grows with distance.
    std::string before = "The quick brown fox jumps over the";
    std::string word = " lazy";
    std::string full = before + word + " dog";
    Frame the = field_frame(rig, before, scale);
    Frame the_quick = field_frame(rig, before + word, scale);
    Frame plain = field_frame(rig, full, scale);
    Frame selected = field_frame(rig, full, scale, std::pair<size_t, size_t>{before.size() + 1, before.size() + word.size()});
    int s = device_scale(plain);

    // Glyph rows, from the plain frame.
    Ink ink{plain};
    int top = -1, bottom = -1;
    for (int y = 0; y < plain.h; ++y)
        for (int x = 0; x < plain.w - 4 * s; ++x)
            if (ink.at(x, y)) { if (top < 0) top = y; bottom = y; break; }
    REQUIRE(top > 0);

    // Each word's ink is what the longer text adds: rendering is left-aligned and positional.
    auto last_ink = [&](const Frame& f) {
        Ink k{f};
        int last = -1;
        for (int x = 0; x < f.w - 4 * s; ++x) if (k.column(x, top, bottom)) last = x;
        return last;
    };
    auto first_ink_after = [&](const Frame& f, int after) {
        Ink k{f};
        for (int x = after + 1; x < f.w - 4 * s; ++x) if (k.column(x, top, bottom)) return x;
        return -1;
    };
    int the_end = last_ink(the);
    int quick_begin = first_ink_after(the_quick, the_end);
    int quick_end = last_ink(the_quick);
    int brown_begin = first_ink_after(plain, quick_end);
    REQUIRE(quick_begin > the_end);
    REQUIRE(brown_begin > quick_end);

    // The highlight's extent, on a row inside it but above the glyphs (the line is taller).
    auto highlight = runs(plain.w, 0, [&](int x) { return selected.differs(plain, x, top - 1); });
    REQUIRE_FALSE(highlight.empty());
    int left = highlight.front().first, right = highlight.back().second;

    INFO("scale " << scale << ": highlight [" << left << ", " << right << "], word ink ["
         << quick_begin << ", " << quick_end << "], previous word ends " << the_end << ", next starts " << brown_begin);
    int slack = s;   // one device pixel of antialiasing either way
    CHECK(left <= quick_begin + slack);
    CHECK(right >= quick_end - slack);
    CHECK(left > the_end - slack);
    CHECK(right < brown_begin + slack);
}

struct RowModel {
    Field<TreeRow> row;
    void view(WidgetTree::ViewBuilder& vb) { vb.widget(row); }
};

Frame row_frame(Rig& rig, std::string label, float scale) {
    RowModel model;
    model.row.set({.id = 1, .label = std::move(label), .icon = std::string{"\xef\x81\xbb"},
                   .depth = 0, .has_children = true});
    WidgetTree tree{model};
    std::string tag = "tree-row@" + std::to_string(static_cast<int>(scale)) + "x-" + std::to_string(rig.version);
    return rig.render<RowModel>(tree, scale, tag.c_str());
}

void check_icon_clear_of_label(float scale, Via via = Via::Sdl) {
    Rig rig{via};
    Frame icon_only = row_frame(rig, "", scale);
    Frame with_label = row_frame(rig, "examples", scale);
    int s = device_scale(icon_only);
    Ink ink{icon_only};
    int band = 24 * s;
    int icon_end = -1;
    for (int x = 0; x < icon_only.w - 4 * s; ++x) if (ink.column(x, 0, band)) icon_end = x;
    int label_begin = -1;
    for (int x = 0; x < with_label.w && label_begin < 0; ++x)
        for (int y = 0; y < band; ++y)
            if (with_label.differs(icon_only, x, y)) { label_begin = x; break; }
    INFO("scale " << scale << ": icon ink ends " << icon_end << ", label ink begins " << label_begin);
    REQUIRE(icon_end > 0);
    REQUIRE(label_begin > 0);
    CHECK(label_begin - icon_end >= 2 * s);
}

} // namespace

// In a Hebrew text field, an English word after the Hebrew one is drawn first, on the left (a
// right-to-left line), and in its own left-to-right order. The window's renderer once shaped the
// whole string as one run, which drew the word backwards.
void check_english_word_in_hebrew_line(float scale) {
    Rig rig;
    const std::string hebrew = "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d";
    Frame word = field_frame(rig, "ab", scale);
    Frame line = field_frame(rig, hebrew + " ab", scale);
    int s = device_scale(word);
    auto ab_w = rig.fonts.rasterize("ab", 14.f * s, FontRole::Ui)->width;
    int x0 = 4 * s, y0 = 6 * s, y1 = 24 * s;   // the field's text, inside its padding
    int differing = 0;
    for (int x = x0; x < x0 + ab_w; ++x)
        for (int y = y0; y < y1; ++y) differing += line.differs(word, x, y) ? 1 : 0;
    CHECK(differing == 0);
}

TEST_CASE("an English word in a Hebrew line is drawn on the left, in order, at 1x") { check_english_word_in_hebrew_line(1.f); }
TEST_CASE("an English word in a Hebrew line is drawn on the left, in order, at 2x") { check_english_word_in_hebrew_line(2.f); }

TEST_CASE("a selection covers exactly its word, at 1x") { check_selection_covers_its_word(1.f); }
TEST_CASE("a selection covers exactly its word, at 2x (HiDPI)") { check_selection_covers_its_word(2.f); }
TEST_CASE("a tree row's icon is clear of its label, at 1x") { check_icon_clear_of_label(1.f); }
TEST_CASE("a tree row's icon is clear of its label, at 2x") { check_icon_clear_of_label(2.f); }

#ifdef PRISM_TEST_WEBGPU
// The same properties through the WebGPU renderer: text measured by SDL_ttf must land where the
// GPU draws it, at 1x and 2x.
TEST_CASE("webgpu: a selection covers exactly its word, at 1x") { check_selection_covers_its_word(1.f, Via::WebGpu); }
TEST_CASE("webgpu: a selection covers exactly its word, at 2x (HiDPI)") { check_selection_covers_its_word(2.f, Via::WebGpu); }
TEST_CASE("webgpu: a tree row's icon is clear of its label, at 1x") { check_icon_clear_of_label(1.f, Via::WebGpu); }
TEST_CASE("webgpu: a tree row's icon is clear of its label, at 2x") { check_icon_clear_of_label(2.f, Via::WebGpu); }
#endif
