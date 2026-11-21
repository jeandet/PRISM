// prism-trace: the host half of PRISM's profiler (doc/design/profiling.md). Reads a profile
// dump -- a file, or a serial device (a path under /dev/, or a COM port name) through
// io::SerialPort when prism-io is built -- and writes Chrome trace-event JSON that
// ui.perfetto.dev opens.
//
//   prism-trace <dump file | serial device> [-o out.json] [--baud N]
//
// A serial read ends when the dump's end chunk has arrived or 5 s pass without bytes. Warnings
// (a damaged chunk, a missing end chunk) and a one-line summary go to stderr. When the input
// holds no dump at all (no sync marker -- a wrong file, or a serial read that timed out before
// the device sent anything) the tool prints an error and exits 1 without writing the output
// file, so a script can tell that from a good conversion.
#include "chrome_json.hpp"
#include "trace_reader.hpp"

#include <prism/io/byte_stream.hpp>
#include <prism/io/file_stream.hpp>
#if defined(PRISM_TRACE_HAS_IO)
#include <prism/io/streams.hpp>

#include <chrono>
#endif

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

void usage()
{
    std::cerr << "usage: prism-trace <dump file | serial device> [-o out.json] [--baud N]\n";
}

std::expected<std::vector<std::byte>, std::error_code> read_file(std::string_view path)
{
    auto file = prism::io::FileStream::open(std::string{path}, prism::io::FileStream::Mode::read);
    if (!file)
        return std::unexpected(file.error());
    std::vector<std::byte> bytes;
    std::array<std::byte, 4096> buf{};
    for (;;) {
        auto got = file->read_some(std::span{buf});
        if (!got)
            return std::unexpected(got.error());
        if (*got == 0)
            return bytes; // EOF
        bytes.insert(bytes.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(*got));
    }
}

#if defined(PRISM_TRACE_HAS_IO)

// The dump's end chunk is the last thing the device sends: an empty payload, so its framing is
// fixed -- type 0x04, length 0, then the CRC-32 of those five bytes. Once the bytes end with
// that, the dump is complete.
bool ends_with_end_chunk(std::span<const std::byte> bytes)
{
    static constexpr std::array<std::byte, 5> end_head{std::byte{0x04}, std::byte{0}, std::byte{0},
                                                       std::byte{0}, std::byte{0}};
    static constexpr std::uint32_t end_crc = prism::profile::crc32(end_head);
    if (bytes.size() < 9)
        return false;
    const std::span<const std::byte> tail = bytes.subspan(bytes.size() - 9);
    if (!std::equal(end_head.begin(), end_head.end(), tail.begin()))
        return false;
    std::uint32_t crc = 0;
    for (std::size_t i = 0; i < 4; ++i)
        crc |= static_cast<std::uint32_t>(tail[5 + i]) << (8 * i);
    return crc == end_crc;
}

// Waits as long as it takes for the first byte (the user presses the board's button after
// starting this), then reads until the dump's end chunk has arrived, the port closes, or 5 s
// pass without bytes (the port's read timeout): the device gone quiet means the dump is over.
std::expected<std::vector<std::byte>, std::error_code> read_serial(std::string_view device, unsigned baud)
{
    prism::io::SerialPort::Options options;
    options.baud = baud;
    options.read_timeout = std::chrono::milliseconds{5000};
    auto port = prism::io::SerialPort::open(std::string{device}, options);
    if (!port)
        return std::unexpected(port.error());
    std::vector<std::byte> bytes;
    std::array<std::byte, 4096> buf{};
    for (;;) {
        auto got = port->read_some(std::span{buf});
        if (!got) {
            if (got.error() == std::errc::timed_out && bytes.empty())
                continue; // nothing sent yet: keep waiting
            if (got.error() == std::errc::timed_out)
                return bytes; // 5 s of silence after the dump started
            return std::unexpected(got.error());
        }
        if (*got == 0)
            return bytes; // the device closed the port
        bytes.insert(bytes.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(*got));
        if (ends_with_end_chunk(bytes))
            return bytes;
    }
}
#endif // PRISM_TRACE_HAS_IO

std::expected<void, std::error_code> write_file(std::string_view path, std::string_view text)
{
    auto file = prism::io::FileStream::open(std::string{path}, prism::io::FileStream::Mode::write);
    if (!file)
        return std::unexpected(file.error());
    if (auto res = file->write(prism::io::as_bytes(text)); !res)
        return std::unexpected(res.error());
    file->close();
    return {};
}

} // namespace

int main(int argc, char** argv)
{
    std::string_view input;
    std::string output_path;
    unsigned baud = 921600; // the board's UART (doc/design/profiling.md)
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "-o" || arg == "--baud") {
            if (i + 1 >= argc) {
                std::cerr << "prism-trace: " << arg << " needs a value\n";
                usage();
                return 2;
            }
            const std::string_view value = argv[++i];
            if (arg == "-o") {
                output_path = value;
            } else {
                const char* const end = value.data() + value.size();
                const auto [ptr, ec] = std::from_chars(value.data(), end, baud);
                if (ec != std::errc{} || ptr != end) {
                    std::cerr << "prism-trace: bad --baud '" << value << "'\n";
                    return 2;
                }
            }
        } else if (arg == "-h" || arg == "--help") {
            usage();
            return 0;
        } else if (input.empty()) {
            input = arg;
        } else {
            std::cerr << "prism-trace: unexpected argument '" << arg << "'\n";
            usage();
            return 2;
        }
    }
    if (input.empty()) {
        usage();
        return 2;
    }

    std::vector<std::byte> bytes;
    if (prism_trace::is_serial_path(input)) {
#if defined(PRISM_TRACE_HAS_IO)
        std::cerr << "prism-trace: waiting for a dump on " << input
                  << " (press the board's button; Ctrl-C to quit)\n";
        auto read = read_serial(input, baud);
        if (!read) {
            std::cerr << "prism-trace: " << input << ": " << read.error().message() << '\n';
            return 1;
        }
        bytes = std::move(*read);
#else
        std::cerr << "prism-trace: serial input needs a build with -Dio=enabled: " << input << '\n';
        return 2;
#endif
    } else {
        auto read = read_file(input);
        if (!read) {
            std::cerr << "prism-trace: " << input << ": " << read.error().message() << '\n';
            return 1;
        }
        bytes = std::move(*read);
    }

    const prism_trace::Trace trace = prism_trace::read_trace(bytes);
    for (const std::string& warning : trace.warnings)
        std::cerr << "prism-trace: warning: " << warning << '\n';
    if (!trace.found) {
        // Not a dump (a wrong file, or a serial read that timed out with no bytes): exit 1 and
        // write no output file, so a script can tell this from a good conversion (I4).
        std::cerr << "prism-trace: " << input << ": no profile dump found\n";
        return 1;
    }
    std::size_t events = 0;
    for (const prism_trace::Trace::Thread& thread : trace.threads)
        events += thread.events.size();
    // ticks_per_second says which clock the device recorded with: its CPU clock when the DWT
    // cycle counter ran (280000000 on the STM32H7B3I-DK), 1000000 for the microsecond fallback
    // (QEMU, where the DWT does not count). Exact in the double, so print it as an integer.
    std::cerr << "prism-trace: " << trace.threads.size() << " threads, " << events << " events, "
              << trace.warnings.size() << " warning(s), " << trace.sites.size() << " sites, "
              << static_cast<std::uint64_t>(trace.ticks_per_second) << " ticks/s\n";

    const std::string json = prism_trace::to_chrome_json(trace);
    if (output_path.empty()) {
        std::cout << json << '\n';
    } else if (auto res = write_file(output_path, json); !res) {
        std::cerr << "prism-trace: " << output_path << ": " << res.error().message() << '\n';
        return 1;
    }
    return 0;
}
