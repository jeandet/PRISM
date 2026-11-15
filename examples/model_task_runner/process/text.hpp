#pragma once

// What a console has to get right about text, as plain functions: cutting a byte stream into
// lines across chunk boundaries, dropping the escape sequences a compiler colours its output
// with, and writing a number the way a console writes it.
//
// Nothing here knows about threads, I/O or the UI.

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prism::runner {

inline char lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

inline bool contains_icase(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    if (needle.size() > haystack.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        std::size_t j = 0;
        while (j < needle.size() && lower(haystack[i + j]) == lower(needle[j])) ++j;
        if (j == needle.size()) return true;
    }
    return false;
}

// Drops the escape sequences a compiler colours its output with, plus the other control bytes
// (a bare '\r' from a redrawing progress bar among them): what a console should show.
inline std::string strip_ansi(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c != 0x1b) {
            if (c >= 0x20 || c == '\t') out.push_back(text[i]);
            continue;
        }
        if (++i >= text.size()) break;
        if (text[i] == '[') {                     // CSI: parameters, then one final byte
            for (++i; i < text.size(); ++i)
                if (text[i] >= '@' && text[i] <= '~') break;
        } else if (text[i] == ']') {              // OSC: up to BEL or ESC '\'
            // The terminator is consumed here, ESC '\' body included: leaving the '\' behind
            // would put it in the output.
            while (++i < text.size()) {
                if (text[i] == '\a') break;
                if (text[i] == 0x1b && i + 1 < text.size() && text[i + 1] == '\\') {
                    ++i;
                    break;
                }
            }
        }
    }
    return out;
}

// A byte stream cut into lines. Chunks are arbitrary: a line may be split across any number of
// them, and the last one may never be terminated at all.
class LineAssembler {
public:
    // Every line this chunk completes; the tail of a split line stays pending.
    std::vector<std::string> feed(std::string_view chunk) {
        std::vector<std::string> lines;
        std::size_t from = 0;
        for (auto nl = chunk.find('\n', from); nl != std::string_view::npos; nl = chunk.find('\n', from)) {
            pending_.append(chunk.substr(from, nl - from));
            lines.push_back(take_pending());
            from = nl + 1;
        }
        pending_.append(chunk.substr(from));
        return lines;
    }

    // What the writer left unterminated when the stream ended.
    std::vector<std::string> finish() {
        if (pending_.empty()) return {};
        return {take_pending()};
    }

private:
    std::string take_pending() {
        if (!pending_.empty() && pending_.back() == '\r') pending_.pop_back();
        std::string line = std::exchange(pending_, {});
        return line;
    }

    std::string pending_{};
};

// Bytes in, finished and cleaned lines out: the one call a run's reader needs.
inline void decode_chunk(LineAssembler& assembler, std::string_view chunk, std::vector<std::string>& out) {
    for (auto& line : assembler.feed(chunk))
        out.push_back(strip_ansi(line));
}

// 200000 reads as 200 000: these numbers are the point of the console.
inline std::string grouped(std::size_t value) {
    std::string text = std::to_string(value);
    for (auto at = static_cast<std::ptrdiff_t>(text.size()) - 3; at > 0; at -= 3)
        text.insert(static_cast<std::size_t>(at), " ");
    return text;
}

} // namespace prism::runner
