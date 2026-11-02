#pragma once

#include <SheenBidi/SheenBidi.h>

#include <cstddef>
#include <string_view>
#include <vector>

// Bidirectional text (UAX #9): where a line's left-to-right and right-to-left runs go on screen.
// One interface, implemented with SheenBidi (chosen at build time, doc/review-2026-11-02.md §4).
// UTF-8 in, byte offsets out.
namespace prism::ui {

// A paragraph's direction. Auto: from its first strong letter (UAX #9 rules P2-P3), left to
// right when there is none. A wrapped line passes its paragraph's, not its own guess.
enum class TextDirection { Auto, Ltr, Rtl };

// A byte range of a line drawn in one direction.
struct BidiRun {
    std::size_t begin = 0, end = 0;
    bool rtl = false;
    bool operator==(const BidiRun&) const = default;
};

namespace bidi_detail {
inline SBLevel base_level(TextDirection dir) {
    return dir == TextDirection::Auto ? SBLevelDefaultLTR : dir == TextDirection::Rtl ? 1 : 0;
}

// Appends the runs of the paragraph starting at `offset`; returns its length.
inline std::size_t append_paragraph(SBAlgorithmRef algo, std::size_t offset, std::size_t rest,
                                    TextDirection dir, std::vector<BidiRun>& runs) {
    SBParagraphRef para = SBAlgorithmCreateParagraph(algo, offset, rest, base_level(dir));
    std::size_t length = SBParagraphGetLength(para);
    SBLineRef line = SBParagraphCreateLine(para, offset, length);
    const SBRun* r = SBLineGetRunsPtr(line);
    for (SBUInteger i = 0, n = SBLineGetRunCount(line); i < n; ++i)
        runs.push_back({r[i].offset, r[i].offset + r[i].length, (r[i].level & 1) != 0});
    SBLineRelease(line);
    SBParagraphRelease(para);
    return length;
}
} // namespace bidi_detail

// The runs of `line`, in visual order (leftmost first), laid out in direction `dir`. A newline
// starts a new paragraph; their runs follow one another.
inline std::vector<BidiRun> bidi_runs(std::string_view line, TextDirection dir = TextDirection::Auto) {
    std::vector<BidiRun> runs;
    if (line.empty()) return runs;
    SBCodepointSequence seq{SBStringEncodingUTF8, line.data(), line.size()};
    SBAlgorithmRef algo = SBAlgorithmCreate(&seq);
    for (std::size_t at = 0; at < line.size();)
        at += bidi_detail::append_paragraph(algo, at, line.size() - at, dir, runs);
    SBAlgorithmRelease(algo);
    return runs;
}

// The direction of the paragraph `text` starts (up to its first newline): Rtl when its first
// strong letter is right to left.
inline TextDirection paragraph_direction(std::string_view text) {
    if (text.empty()) return TextDirection::Ltr;
    SBCodepointSequence seq{SBStringEncodingUTF8, text.data(), text.size()};
    SBAlgorithmRef algo = SBAlgorithmCreate(&seq);
    SBParagraphRef para = SBAlgorithmCreateParagraph(algo, 0, text.size(), SBLevelDefaultLTR);
    bool rtl = (SBParagraphGetBaseLevel(para) & 1) != 0;
    SBParagraphRelease(para);
    SBAlgorithmRelease(algo);
    return rtl ? TextDirection::Rtl : TextDirection::Ltr;
}

// True when every run is left to right: the line draws and measures as it always did.
inline bool is_plain_ltr(const std::vector<BidiRun>& runs) {
    return runs.size() <= 1 && (runs.empty() || !runs[0].rtl);
}

} // namespace prism::ui
