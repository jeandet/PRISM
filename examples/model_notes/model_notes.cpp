// Notes: a small text editor -- a directory tree, a file editor, search, save. The text-heavy
// reference app: it exercises editing, selection, clipboard, undo, IME and text measurement the
// way a dashboard never does. Logic lives in notes_logic.hpp (tested); this file only wires it.
#include <prism/prism.hpp>
#include <prism/app/settings.hpp>
#include <prism/backends/settings_dir.hpp>
#include "file_tree_source.hpp"
#include "../showcase/showcase_common.hpp"
#include "notes_logic.hpp"

#include <filesystem>
#include <vector>

using namespace prism;

namespace fs = std::filesystem;

// What the app remembers between runs (app/settings.hpp): saved as settings.toml.
struct NotesPrefs {
    core::Field<std::string> last_file{""};
    core::Field<ui::SplitSizes> sidebar{};   // the file tree's width and the editor's
    app::WindowGeometry window;
};

// A second window (AppContext::open_window): live statistics of the text being edited, read
// from the main window's field.
struct NotesStats {
    core::Derived<std::string> summary;
    explicit NotesStats(core::Field<ui::TextArea<>>& body)
        : summary([&body] {
              auto s = notes::stats(value(body));
              return std::to_string(s.lines) + " lines, " + std::to_string(s.characters) + " characters";
          }, body) {}
    void view(app::ViewBuilder& vb) { vb.widget(summary); }
};

struct NotesApp {
    NotesPrefs prefs;
    FileTreeSource source;
    ui::TreeController files{ui::wrap_tree_storage(source)};
    core::Field<ui::Menu> file_menu{{.title = "&File", .items = {
        {.text = "&Open…", .shortcut = {ui::keys::o, ui::mods::ctrl}},
        {.text = "&Save", .shortcut = {ui::keys::s, ui::mods::ctrl}, .enabled = false},
        {.text = "Save &As…", .shortcut = {ui::keys::s, ui::mods::ctrl | ui::mods::shift}},
        {.separator = true},
        {.text = "Re&name…", .shortcut = {ui::keys::f2, 0}},
        {.text = "&Reload", .shortcut = {ui::keys::f5, 0}},
    }}};
    core::Field<ui::Menu> search_menu{{.title = "&Search", .items = {{"&Clear search"}}}};
    core::Field<ui::Menu> view_menu{{.title = "&View", .items = {
        {.text = "&Statistics window", .shortcut = {ui::keys::i, ui::mods::ctrl}}}}};
    core::Field<ui::ContextMenu> editor_menu{{.items = {{.text = "Save", .enabled = false}, {"Reload"}, {"Clear search"}}}};
    core::Field<bool> modified{false};   // the text differs from the file: Save is enabled
    core::Field<ui::Dialog> discard{{.title = "Unsaved changes",
                                         .message = "Reloading discards your edits.",
                                         .buttons = {"Discard", "Cancel"}}};
    core::Field<ui::TextField<>> search{{.placeholder = "Find in file..."}};
    core::Field<ui::Button> save{{.text = "Save", .primary = false}};
    core::Field<ui::Button> reload{{.text = "Reload", .primary = false}};
    core::Field<ui::TextArea<>> body{{.placeholder = "Select a text file on the left", .rows = 26}};
    core::Field<ui::Label<>> status{{"No file open"}};
    NotesStats stats{body};
    app::WindowId stats_window = 0;
    // File > Rename…: a dialog holding a field
    core::Field<ui::Dialog> rename_dialog{{.title = "Rename", .message = "", .buttons = {"Rename", "Cancel"}}};
    core::Field<ui::TextField<>> new_name{{.placeholder = "New file name"}};

    fs::path current;
    fs::path pending_reload;   // the file the "Unsaved changes" dialog is about
    std::string saved;   // the file as last loaded or saved: tells whether `body` was modified
    std::string error;   // last load/save problem, shown in the status line
    core::Wiring wire;   // captures `this`: NotesApp is never moved

    explicit NotesApp(fs::path root) : source(std::move(root)) {
        wire.on(files.selected, [this](const auto& id) {
            std::error_code ec;
            if (id && fs::is_regular_file(source.path_of(*id), ec)) open(source.path_of(*id));
        });
        wire.on(body, [this](const auto&) { update_matches(); });
        wire.on(search, [this](const auto&) { update_matches(); });
        wire.on(save, [this](const auto&) { write(); });
        auto run_from = [this](auto& menu) {
            return ui::on_choice(menu, [this, &menu](std::size_t i) { run(ui::menu_text(menu.get().items[i])); });
        };
        wire.keep(run_from(file_menu));
        wire.keep(run_from(search_menu));
        wire.keep(run_from(view_menu));
        wire.keep(run_from(editor_menu));
        wire.on(modified, [this](bool on) {
            enable_item(file_menu, "Save", on);
            enable_item(editor_menu, "Save", on);
        });
        wire.on(reload, [this](const auto&) { run("Reload"); });
        wire.on(rename_dialog, [this](const ui::Dialog& d) {
            if (!d.open && d.chosen == 0) rename_current();
        });
        wire.on(discard, [this](const ui::Dialog& d) {
            if (!d.open && d.chosen == 0 && !pending_reload.empty()) open(pending_reload);
        });
    }

    app::AppContext* ctx = nullptr;   // for the platform's file dialogs; set by main()

    // Every menu entry, by its label.
    void run(std::string_view command) {
        if (command == "Open…") ask_for_file(app::FileDialogKind::Open);
        else if (command == "Save As…") ask_for_file(app::FileDialogKind::Save);
        else if (command == "Rename…" && !current.empty()) {
            new_name.set({.value = current.filename().string(), .placeholder = new_name.get().placeholder});
            auto d = rename_dialog.get();
            d.open = true;
            rename_dialog.set(d);
        }
        else if (command == "Save") write();
        else if (command == "Reload" && !current.empty()) confirm_reload();
        else if (command == "Clear search") search.set({.placeholder = search.get().placeholder});
        else if (command == "Statistics window" && ctx && !ctx->window_open(stats_window))
            stats_window = ctx->open_window(stats, {.title = "Statistics", .width = 320, .height = 80});
    }

    void view(app::ViewBuilder& vb) {
        vb.hstack([&] {
            vb.remember_split(prefs.sidebar);
            vb.tree(files);
            vb.handle();
            vb.vstack([&] {
                vb.hstack([&] {
                    vb.widget(file_menu);
                    vb.widget(search_menu);
                    vb.widget(view_menu);
                    vb.spacer();
                });
                vb.toolbar([&] {
                    vb.widget(save).enabled(modified).tooltip("Write the file to disk (Ctrl+S)");
                    vb.widget(reload).tooltip("Read the file again, dropping unsaved edits");
                    vb.widget(search).tooltip("Highlights every match in the file");
                });
                vb.widget(body);
                vb.spacer();
                vb.status_bar([&] { vb.widget(status); vb.spacer(); });
                vb.context_menu(editor_menu);
                vb.dialog(discard);
                vb.dialog(rename_dialog, [&](app::ViewBuilder& d) { d.widget(new_name).focus_on_open(); });
            });
        });
    }

    void open(const fs::path& path) {
        auto text = notes::load_text(path);
        if (!text) {
            error = path.filename().string() + ": " + text.error();
            refresh_status();
            return;
        }
        current = path;
        saved = *text;
        error.clear();
        prefs.last_file.set(fs::absolute(path).string());
        body.set({.value = *text, .placeholder = body.get().placeholder, .rows = body.get().rows});
        refresh_status();
    }

    // The platform's file dialog; its answer comes back on the logic thread.
    void ask_for_file(app::FileDialogKind kind) {
        if (!ctx) return;
        bool saving = kind == app::FileDialogKind::Save;
        app::FileDialog dialog{.kind = kind,
                               .title = saving ? "Save the note as" : "Open a note",
                               .filters = {{"Text", "txt;md;cpp;hpp;py;toml;json;yaml"}, {"Any file", "*"}},
                               // a folder to open from, the file itself to save over; empty: the platform picks
                               .start = (saving ? current : current.parent_path()).string()};
        ctx->choose_files(std::move(dialog), [this, saving](app::FileDialogResult r) {
            if (!r.error.empty()) {
                error = "file dialog: " + r.error;
                refresh_status();
            } else if (!r.paths.empty() && saving) {
                save_as(r.paths.front());
            } else if (!r.paths.empty()) {
                open(r.paths.front());
            }
        });
    }

    void rename_current() {
        const auto& name = value(new_name);
        if (current.empty() || name.empty() || name == current.filename().string()) return;
        auto to = current.parent_path() / name;
        std::error_code ec;
        fs::rename(current, to, ec);
        if (ec) error = "rename failed: " + ec.message();
        else {
            current = to;
            error.clear();
        }
        refresh_status();
    }

    // Writes to `path` and makes it the current file -- only if the write succeeded.
    void save_as(const fs::path& path) {
        if (auto ok = notes::save_text(path, value(body)); !ok) {
            error = "save failed: " + ok.error();
        } else {
            current = path;
            saved = value(body);
            error.clear();
        }
        refresh_status();
    }

    void confirm_reload() {
        if (value(body) == saved) return open(current);
        pending_reload = current;
        auto d = discard.get();
        d.open = true;
        d.chosen.reset();
        discard.set(d);
    }

    void write() {
        if (current.empty()) return;
        if (auto ok = notes::save_text(current, value(body)); !ok) error = "save failed: " + ok.error();
        else { saved = value(body); error.clear(); }
        refresh_status();
    }

    // Highlights every match of the search text in the editor (stored in the field's value, so the
    // TextArea draws them), then refreshes the status line. Setting the value notifies again;
    // the second pass finds identical highlights and stops.
    void update_matches() {
        const auto& query = value(search);
        std::vector<ui::TextRange> hits;
        for (auto pos : notes::find_all(value(body), query)) hits.push_back({pos, pos + query.size()});
        if (hits != body.get().highlights) {
            auto b = body.get();
            b.highlights = std::move(hits);
            body.set(b);
        }
        refresh_status();
    }

    void refresh_status() {
        const auto& text = value(body);
        auto query = value(search);
        std::optional<std::size_t> matches;
        if (!query.empty()) matches = notes::find_all(text, query).size();
        auto line = notes::status_line(current.filename().string(), notes::stats(text), text != saved, matches);
        status.set({error.empty() ? line : error});
        modified.set(!current.empty() && text != saved);
    }

    template <typename M>
    static void enable_item(core::Field<M>& menu, std::string_view text, bool on) {
        auto m = menu.get();
        for (auto& item : m.items)
            if (ui::menu_text(item) == text) item.enabled = on;
        menu.set(m);
    }
};

int main(int argc, char* argv[]) {
    if (argc >= 2 && notes::is_capture_path(argv[1])) {   // README capture: open a known file
        NotesApp app{argc >= 3 ? fs::path(argv[2]) : fs::current_path()};
        if (argc >= 4) app.open(argv[3]);
        app.search.set({.value = "include", .placeholder = "Find in file..."});   // show highlights
        if (app.files.rows.size() > 0) app.files.on_row_clicked(0, app.files.rows[0]);
        return showcase(argc, argv, app, 1000, 640);
    }
    NotesApp app{argc >= 2 ? fs::path(argv[1]) : fs::current_path()};
    // Loads the last session's preferences, and saves them as they change.
    app::SettingsFile settings{app.prefs, backends::settings_dir("prism", "notes") / "settings.toml"};
    if (std::error_code ec; fs::is_regular_file(app.prefs.last_file.get(), ec)) app.open(app.prefs.last_file.get());
    auto backend = app::Backend::software(app::RenderConfig{});
    auto& window = backend.create_window(app.prefs.window.applied_to(
        {.title = "PRISM Notes", .width = 1000, .height = 640, .decoration = app::DecorationMode::Custom}));
    app::model_app(backend, window, app, [&app](app::AppContext& ctx) {
        app.ctx = &ctx;
        ctx.remember_window_size(app.prefs.window);
        ctx.on_file_drop([&app](const input::FileDrop& drop) { app.open(drop.paths.front()); });
    });
    app.ctx = nullptr;   // the context ended with model_app
}
