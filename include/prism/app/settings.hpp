#pragma once

#include <prism/core/connection.hpp>
#include <prism/core/field.hpp>
#include <prism/core/traits.hpp>
#include <prism/delegates/form_delegates.hpp>
#include <prism/ui/delegate.hpp>

#include <glaze/toml.hpp>

#include <chrono>
#include <condition_variable>
#include <expected>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// Settings: a struct of fields saved as a TOML file, through Glaze (github.com/stephenberry/glaze).
// Each field is one key; a nested struct is a [table]. A widget field keeps only its value (a
// slider's position, a checkbox's mark, a text field's text), and an enum is written by name.
// A settings struct must not be in an anonymous namespace: Glaze's reflection needs a type
// with linkage under clang.
namespace prism::app {

namespace settings_detail {

template <typename T>
concept Enum = std::is_scoped_enum_v<T>;

// The part of a field's value that is a setting.
template <typename T>
auto& setting_of(T& v) {
    static_assert(!std::is_same_v<std::remove_const_t<T>, ui::Password<>>, "a password does not belong in a settings file");
    if constexpr (std::is_arithmetic_v<T> || std::is_same_v<std::remove_const_t<T>, std::string> || Enum<std::remove_const_t<T>>)
        return v;
    else if constexpr (requires { v.checked; })
        return v.checked;
    else
        return v.value;
}

template <typename T>
using setting_t = std::remove_cvref_t<decltype(setting_of(std::declval<T&>()))>;

// How a setting is stored: as itself, or an enum as its name.
template <typename V>
using stored_t = std::conditional_t<Enum<V>, std::string, V>;

template <typename V>
stored_t<V> to_stored(const V& v) {
    if constexpr (Enum<V>) return ui::enum_label<V>(ui::enum_index(v));
    else return v;
}

// False when the stored value names no enum value (an old or mistyped file): then it is skipped.
template <typename V>
bool from_stored(V& out, const stored_t<V>& s) {
    if constexpr (Enum<V>) {
        for (std::size_t i = 0; i < ui::enum_count<V>(); ++i)
            if (ui::enum_label<V>(i) == s) {
                out = ui::enum_from_index<V>(i);
                return true;
            }
        return false;
    } else {
        out = s;
        return true;
    }
}

template <typename T>
void put(core::Field<T>& f, const stored_t<setting_t<T>>& s) {
    T v = f.get();
    if (from_stored(setting_of(v), s)) f.set(std::move(v));   // set(): widgets and observers follow
}

template <typename T>
stored_t<setting_t<T>> get(const core::Field<T>& f) {
    return to_stored(setting_of(f.get()));
}

inline constexpr glz::opts read_opts{.format = glz::TOML, .error_on_unknown_keys = false};

} // namespace settings_detail

template <typename S>
std::string to_toml(const S& settings) {
    std::string out;
    if (auto ec = glz::write_toml(settings, out)) return {};
    return out;
}

// Sets every field the text names. Unknown keys are skipped: a file from another version loads.
template <typename S>
std::expected<void, std::string> from_toml(S& settings, std::string_view text) {
    std::string buffer{text};   // Glaze reads from a null-terminated buffer
    if (auto ec = glz::read<settings_detail::read_opts>(settings, buffer))
        return std::unexpected(glz::format_error(ec, buffer));
    return {};
}

// A missing file is not an error: on the first run there is none yet.
template <typename S>
std::expected<void, std::string> load_settings(S& settings, const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) return {};
    std::stringstream text;
    text << in.rdbuf();
    auto r = from_toml(settings, text.str());
    if (!r) return std::unexpected(file.string() + ": " + r.error());
    return {};
}

namespace settings_detail {
// Through a temporary file and a rename, so a crash mid-write leaves the old file whole.
inline std::expected<void, std::string> write_file(const std::filesystem::path& file, const std::string& text) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    auto tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!(out << text)) return std::unexpected("cannot write " + tmp.string());
    }
    std::filesystem::rename(tmp, file, ec);
    if (ec) return std::unexpected("cannot replace " + file.string() + ": " + ec.message());
    return {};
}
} // namespace settings_detail

template <typename S>
std::expected<void, std::string> save_settings(const S& settings, const std::filesystem::path& file) {
    return settings_detail::write_file(file, to_toml(settings));
}

// Loads `settings` from `file`, then saves it after each change: a short pause gathers a burst
// (a slider being dragged) into one write, done on a thread of its own. Closing it writes the
// last change. Fields are read on the thread that changes them (the logic thread).
// A file that does not parse is kept beside it as "<file>.broken", and the defaults are used.
// simplify: in the browser the file lives in memory and is gone on reload; persisting it needs
// IndexedDB (Emscripten's IDBFS) and a sync after each write.
template <typename S>
class SettingsFile {
public:
    SettingsFile(S& settings, std::filesystem::path file,
                 std::chrono::milliseconds pause = std::chrono::milliseconds{300})
        : settings_(settings), file_(std::move(file)), pause_(pause) {
        if (auto r = load_settings(settings_, file_); !r) {
            load_error_ = r.error();
            std::error_code ec;
            std::filesystem::rename(file_, std::filesystem::path(file_) += ".broken", ec);
        }
        watch(settings_);
        writer_ = std::thread([this] { write_loop(); });
    }

    SettingsFile(const SettingsFile&) = delete;
    SettingsFile& operator=(const SettingsFile&) = delete;

    ~SettingsFile() {
        watches_.clear();   // no new changes from here
        {
            std::lock_guard lk(m_);
            stop_ = true;
        }
        cv_.notify_one();
        writer_.join();   // writes what is pending
    }

    // Why the file did not load, if it did not (empty otherwise).
    [[nodiscard]] const std::string& load_error() const { return load_error_; }

private:
    S& settings_;
    std::filesystem::path file_;
    std::chrono::milliseconds pause_;
    std::string load_error_;
    std::vector<core::Connection> watches_;
    std::mutex m_;
    std::condition_variable cv_;
    std::optional<std::string> pending_;
    bool stop_ = false;
    std::thread writer_;

    template <typename T>
    void watch(T& v) {
        if constexpr (core::is_field_v<T>)
            watches_.push_back(v.on_change().connect([this](const auto&) { changed(); }));
        else if constexpr (glz::reflectable<T>)
            glz::for_each_field(v, [this](auto& member) { this->watch(member); });
    }

    void changed() {
        auto text = to_toml(settings_);
        {
            std::lock_guard lk(m_);
            pending_ = std::move(text);
        }
        cv_.notify_one();
    }

    void write_loop() {
        std::unique_lock lk(m_);
        for (;;) {
            cv_.wait(lk, [this] { return pending_ || stop_; });
            if (!stop_) cv_.wait_for(lk, pause_, [this] { return stop_; });   // gather the burst
            auto text = std::exchange(pending_, std::nullopt);
            if (text) {
                lk.unlock();
                (void)settings_detail::write_file(file_, *text);
                lk.lock();
            }
            if (stop_ && !pending_) return;
        }
    }
};

} // namespace prism::app

// A Field is written as its setting (see settings_detail::setting_of), and reading one sets it.
template <typename T>
struct glz::meta<prism::core::Field<T>> {
    using stored = prism::app::settings_detail::stored_t<prism::app::settings_detail::setting_t<T>>;
    static constexpr auto value = glz::custom<
        [](prism::core::Field<T>& f, const stored& s) { prism::app::settings_detail::put(f, s); },
        [](const prism::core::Field<T>& f) { return prism::app::settings_detail::get(f); }>;
};
