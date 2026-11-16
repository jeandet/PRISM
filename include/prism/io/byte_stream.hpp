#pragma once

#include <concepts>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

// A byte stream, PRISM's QIODevice: what a TCP socket, a serial port, a pipe or a test double
// share. Blocking, for a worker thread; what it reads crosses into the model through a Shared or
// a Channel like anything else. A read waits at most the stream's own timeout and then fails with
// std::errc::timed_out, so a silent peer never hangs the thread for good.
namespace prism::io {

template <typename S>
concept ByteStream = requires(S& s, std::span<const std::byte> out, std::span<std::byte> in) {
    { s.write(out) } -> std::same_as<std::expected<void, std::error_code>>;          // all of it
    { s.read_some(in) } -> std::same_as<std::expected<std::size_t, std::error_code>>; // 0: closed
    { s.close() } -> std::same_as<void>;
};

inline std::span<const std::byte> as_bytes(std::string_view text) {
    return std::as_bytes(std::span{text.data(), text.size()});
}

// Lines over a byte stream ('\n'-terminated, a '\r' before it dropped): what SCPI instruments, GPS
// receivers and modems speak. Keeps what arrived past the last line for the next call.
template <ByteStream S>
class LineStream {
public:
    explicit LineStream(S& stream) : stream_(&stream) {}

    std::expected<void, std::error_code> write_line(std::string_view line) {
        std::string framed{line};
        framed += '\n';
        return stream_->write(as_bytes(framed));
    }

    std::expected<std::string, std::error_code> read_line() {
        for (;;) {
            if (auto line = take_line()) return *line;
            std::byte chunk[512];
            auto got = stream_->read_some(chunk);
            if (!got) return std::unexpected(got.error());
            if (*got == 0) return std::unexpected(std::make_error_code(std::errc::connection_reset));
            pending_.append(reinterpret_cast<const char*>(chunk), *got);
        }
    }

    // One command, one reply line: SCPI's query.
    std::expected<std::string, std::error_code> query(std::string_view command) {
        if (auto sent = write_line(command); !sent) return std::unexpected(sent.error());
        return read_line();
    }

private:
    std::optional<std::string> take_line() {
        const auto end = pending_.find('\n');
        if (end == std::string::npos) return std::nullopt;
        std::string line = pending_.substr(0, end);
        pending_.erase(0, end + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return line;
    }

    S* stream_;
    std::string pending_;
};

} // namespace prism::io
