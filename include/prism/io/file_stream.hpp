#pragma once

// io::FileStream: a ByteStream (byte_stream.hpp) over a file -- where a desktop dump goes
// (doc/design/profiling.md). Hosted only: src/meson.build compiles src/io/file_stream.cpp only
// when host_machine.system() != 'none'; bare metal has no file system, so there this header
// only declares. The path is a narrow string, which covers the paths PRISM opens.
#include <prism/io/byte_stream.hpp>

#include <cstdio>
#include <expected>
#include <span>
#include <string_view>
#include <system_error>

namespace prism::io {

class FileStream {
public:
    enum class Mode { read, write, append };

    // Opens the file at `path`; fails with the errno-derived error code when it cannot.
    static std::expected<FileStream, std::error_code> open(std::string_view path, Mode mode);

    FileStream(FileStream&& other) noexcept;
    FileStream& operator=(FileStream&& other) noexcept;
    ~FileStream();
    FileStream(const FileStream&) = delete;
    FileStream& operator=(const FileStream&) = delete;

    std::expected<void, std::error_code> write(std::span<const std::byte> bytes);     // all of it
    std::expected<std::size_t, std::error_code> read_some(std::span<std::byte> into); // 0: EOF
    void close();

private:
    explicit FileStream(std::FILE* file) : file_(file) {}
    std::FILE* file_ = nullptr;
};

static_assert(ByteStream<FileStream>);

} // namespace prism::io
