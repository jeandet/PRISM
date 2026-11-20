#pragma once

// io::MemoryStream: a ByteStream (byte_stream.hpp) over a std::vector<std::byte> -- the
// in-memory sink for tests, for a dump built before it is sent, for anything that wants the
// bytes in one piece. Header-only, so it builds everywhere, bare metal included.
#include <prism/io/byte_stream.hpp>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <span>
#include <system_error>
#include <vector>

namespace prism::io {

class MemoryStream {
public:
    std::expected<void, std::error_code> write(std::span<const std::byte> bytes) {
        if (closed_)
            return std::unexpected(std::make_error_code(std::errc::bad_file_descriptor));
        if (!bytes.empty())
            data_.insert(data_.end(), bytes.begin(), bytes.end());
        return {};
    }
    // 0 once everything written has been read (the stream is closed for reading).
    std::expected<std::size_t, std::error_code> read_some(std::span<std::byte> into) {
        if (closed_)
            return std::size_t{0};
        const std::size_t n = std::min(into.size(), data_.size() - read_pos_);
        if (n != 0)
            std::copy_n(data_.data() + read_pos_, n, into.data());
        read_pos_ += n;
        return n;
    }
    void close() { closed_ = true; }

    std::span<const std::byte> bytes() const { return data_; }
    bool closed() const { return closed_; }

private:
    std::vector<std::byte> data_;
    std::size_t read_pos_ = 0;
    bool closed_ = false;
};

static_assert(ByteStream<MemoryStream>);

} // namespace prism::io
