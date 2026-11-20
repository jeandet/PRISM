// dump_to_file (include/prism/profile/dump.hpp): the global recorder's dump written to a file,
// through io::FileStream. Compiled only with -Dprofiling=enabled on a hosted system
// (src/meson.build gates this file on both): bare metal has no file system to write to.
#include <prism/profile/dump.hpp>

#include <prism/io/file_stream.hpp>

#include <expected>
#include <string_view>
#include <system_error>

namespace prism::profile {

std::expected<void, std::error_code> dump_to_file(std::string_view path)
{
    auto file = io::FileStream::open(path, io::FileStream::Mode::write);
    if (!file)
        return std::unexpected(file.error());
    auto dumped = dump(global(), *file);
    file->close();
    if (!dumped)
        return std::unexpected(dumped.error());
    return {};
}

} // namespace prism::profile
