#pragma once

#include <SheenBidi/SheenBidi.h>

#include <cstddef>
#include <string_view>
#include <vector>

// Bidirectional text (UAX #9): where a line's left-to-right and right-to-left runs go on screen.
// One interface, implemented with SheenBidi (chosen at build time, doc/review-2026-11-02.md §4).
// UTF-8 in, byte offsets out.
// simplify: each line is its own paragraph, so a wrapped Hebrew paragraph whose second line
// starts with an English word lays that line out left to right. Upgrade: resolve the paragraph
// once and pass its base level to each line (SBParagraphCreateLine on the wrapped ranges).
namespace prism::ui {

// A byte range of a line drawn in one direction.
struct BidiRun {
    std::size_t begin = 0, end = 0;
    bool rtl = false;
    bool operator==(const BidiRun&) const = default;
};

namespace bidi_detail {
// Appends the runs of the paragraph starting at `offset`; returns its length.
inline std::size_t append_paragraph(SBAlgorithmRef algo, std::size_t offset, std::size_t rest,
                                    std::vector<BidiRun>& runs) {
    SBParagraphRef para = SBAlgorithmCreateParagraph(algo, offset, rest, SBLevelDefaultLTR);
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

// The runs of `line`, in visual order (leftmost first). The base direction comes from the first
// strong character (rules P2-P3), left to right when there is none. A newline starts a new
// paragraph; their runs follow one another.
inline std::vector<BidiRun> bidi_runs(std::string_view line) {
    std::vector<BidiRun> runs;
    if (line.empty()) return runs;
    SBCodepointSequence seq{SBStringEncodingUTF8, line.data(), line.size()};
    SBAlgorithmRef algo = SBAlgorithmCreate(&seq);
    for (std::size_t at = 0; at < line.size();)
        at += bidi_detail::append_paragraph(algo, at, line.size() - at, runs);
    SBAlgorithmRelease(algo);
    return runs;
}

// True when every run is left to right: the line draws and measures as it always did.
inline bool is_plain_ltr(const std::vector<BidiRun>& runs) {
    return runs.size() <= 1 && (runs.empty() || !runs[0].rtl);
}

} // namespace prism::ui
