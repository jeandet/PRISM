// io::ByteStream and io::LineStream (include/prism/io/byte_stream.hpp) over an in-memory stream:
// line framing across chunk boundaries, CRLF, a peer that closes, a timeout.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/io/byte_stream.hpp>

#include <algorithm>
#include <deque>
#include <string>

using namespace prism;

namespace {
// Hands out its input in the chunk sizes it was given, then closes or times out; records writes.
struct ScriptedStream {
    std::deque<std::string> chunks{};
    bool time_out_at_end = false;
    std::string written{};

    std::expected<void, std::error_code> write(std::span<const std::byte> bytes) {
        written.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return {};
    }
    std::expected<std::size_t, std::error_code> read_some(std::span<std::byte> into) {
        if (chunks.empty()) {
            if (time_out_at_end) return std::unexpected(std::make_error_code(std::errc::timed_out));
            return 0;
        }
        auto& chunk = chunks.front();
        const std::size_t n = std::min(chunk.size(), into.size());
        std::copy_n(reinterpret_cast<const std::byte*>(chunk.data()), n, into.data());
        chunk.erase(0, n);
        if (chunk.empty()) chunks.pop_front();
        return n;
    }
    void close() {}
};
static_assert(io::ByteStream<ScriptedStream>);
}

TEST_CASE("lines are cut wherever the chunks fall, and CRLF loses its CR") {
    ScriptedStream s{.chunks = {"12.5,0.", "8\r\nsecond", " line\nthird\n"}};
    io::LineStream lines{s};
    CHECK(lines.read_line().value() == "12.5,0.8");
    CHECK(lines.read_line().value() == "second line");
    CHECK(lines.read_line().value() == "third");
}

TEST_CASE("a query writes the command with its newline, then reads one reply") {
    ScriptedStream s{.chunks = {"5.000\n"}};
    io::LineStream lines{s};
    CHECK(lines.query("MEAS:VOLT?").value() == "5.000");
    CHECK(s.written == "MEAS:VOLT?\n");
}

TEST_CASE("a peer that closes mid-line, or never answers, is an error, not a hang") {
    ScriptedStream closed{.chunks = {"partial"}};
    io::LineStream a{closed};
    CHECK(a.read_line().error() == std::errc::connection_reset);

    ScriptedStream silent{.time_out_at_end = true};
    io::LineStream b{silent};
    CHECK(b.read_line().error() == std::errc::timed_out);
}
