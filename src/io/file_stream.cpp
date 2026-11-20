// io::FileStream (include/prism/io/file_stream.hpp): a ByteStream over a std::FILE. Hosted
// only: src/meson.build compiles this file only when host_machine.system() != 'none' -- bare
// metal has no file system.
#include <prism/io/file_stream.hpp>

#include <cerrno>
#include <string>

namespace prism::io {
namespace {

const char* c_mode(FileStream::Mode mode)
{
    switch (mode) {
    case FileStream::Mode::read: return "rb";
    case FileStream::Mode::write: return "wb";
    case FileStream::Mode::append: return "ab";
    }
    return "rb"; // unreachable
}

std::error_code last_error()
{
    // A failed stdio call leaves the reason in errno; when it does not, report a plain I/O
    // error rather than a zero (success) error code.
    return errno != 0 ? std::error_code{errno, std::generic_category()}
                       : std::make_error_code(std::errc::io_error);
}

} // namespace

FileStream::FileStream(FileStream&& other) noexcept : file_(other.file_) { other.file_ = nullptr; }
FileStream& FileStream::operator=(FileStream&& other) noexcept
{
    if (this != &other) {
        close();
        file_ = other.file_;
        other.file_ = nullptr;
    }
    return *this;
}
FileStream::~FileStream() { close(); }

std::expected<FileStream, std::error_code> FileStream::open(std::string_view path, Mode mode)
{
    const std::string narrow{path}; // fopen needs a NUL-terminated path
    std::FILE* f = std::fopen(narrow.c_str(), c_mode(mode));
    if (f == nullptr)
        return std::unexpected(last_error());
    return FileStream{f};
}

std::expected<void, std::error_code> FileStream::write(std::span<const std::byte> bytes)
{
    if (file_ == nullptr)
        return std::unexpected(std::make_error_code(std::errc::bad_file_descriptor));
    if (!bytes.empty() && std::fwrite(bytes.data(), 1, bytes.size(), file_) != bytes.size())
        return std::unexpected(last_error()); // short write: the error is errno's
    return {};
}

std::expected<std::size_t, std::error_code> FileStream::read_some(std::span<std::byte> into)
{
    if (file_ == nullptr)
        return std::unexpected(std::make_error_code(std::errc::bad_file_descriptor));
    if (into.empty())
        return std::size_t{0};
    const std::size_t got = std::fread(into.data(), 1, into.size(), file_);
    if (got < into.size() && std::ferror(file_) != 0)
        return std::unexpected(last_error());
    return got; // short of into.size() without an error is EOF; 0: closed for reading
}

void FileStream::close()
{
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

} // namespace prism::io
