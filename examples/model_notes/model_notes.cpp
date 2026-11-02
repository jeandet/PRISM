// Notes: a small text editor -- a directory tree, a file editor, search, save. The text-heavy
// reference app: it exercises editing, selection, clipboard, undo, IME and text measurement the
// way a dashboard never does. Logic lives in notes_logic.hpp (tested); this file only wires it.
#include <prism/prism.hpp>
#include "../model_tree_browser/file_tree_source.hpp"
#include "../showcase/showcase_common.hpp"
#include "notes_logic.hpp"

#include <filesystem>
#include <vector>

using namespace prism;

namespace fs = std::filesystem;

struct NotesApp {
    FileTreeSource source;
    ui::TreeController files{ui::wrap_tree_storage(source)};
    core::Field<ui::Menu> file_menu{{.title = "File", .items = {"Save", "Reload"}}};
    core::Field<ui::Menu> search_menu{{.title = "Search", .items = {"Clear search"}}};
    core::Field<ui::ContextMenu> editor_menu{{.items = {"Save", "Reload", "Clear search"}}};
    core::Field<ui::Dialog> discard{{.title = "Unsaved changes",
                                         .message = "Reloading discards your edits.",
                                         .buttons = {"Discard", "Cancel"}}};
    core::Field<ui::TextField<>> search{{.placeholder = "Find in file..."}};
    core::Field<ui::Button> save{{"Save"}};
    core::Field<ui::Button> reload{{"Reload"}};
    core::Field<ui::TextArea<>> body{{.placeholder = "Select a text file on the left", .rows = 26}};
    core::Field<ui::Label<>> status{{"No file open"}};

    fs::path current;
    fs::path pending_reload;   // the file the "Unsaved changes" dialog is about
    std::string saved;   // the file as last loaded or saved: tells whether `body` was modified
    std::string error;   // last load/save problem, shown in the status line
    std::vector<core::Connection> wiring;   // captures `this`: NotesApp is never moved

    explicit NotesApp(fs::path root) : source(std::move(root)) {
        wiring.push_back(files.selected.on_change().connect([this](const auto& id) {
            std::error_code ec;
            if (id && fs::is_regular_file(source.path_of(*id), ec)) open(source.path_of(*id));
        }));
        wiring.push_back(body.on_change().connect([this](const auto&) { update_matches(); }));
        wiring.push_back(search.on_change().connect([this](const auto&) { update_matches(); }));
        wiring.push_back(save.on_change().connect([this](const auto&) { write(); }));
        auto on_choice = [this](const auto& m) { if (m.chosen) run(m.items[*m.chosen]); };
        wiring.push_back(file_menu.on_change().connect(on_choice));
        wiring.push_back(search_menu.on_change().connect(on_choice));
        wiring.push_back(editor_menu.on_change().connect(on_choice));
        wiring.push_back(reload.on_change().connect([this](const auto&) { run("Reload"); }));
        wiring.push_back(discard.on_change().connect([this](const ui::Dialog& d) {
            if (!d.open && d.chosen == 0 && !pending_reload.empty()) open(pending_reload);
        }));
    }

    // Every menu entry, by its label.
    void run(std::string_view command) {
        if (command == "Save") write();
        else if (command == "Reload" && !current.empty()) confirm_reload();
        else if (command == "Clear search") search.set({.placeholder = search.get().placeholder});
    }

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.hstack([&] {
            vb.tree(files);
            vb.handle();
            vb.vstack([&] {
                vb.hstack(file_menu, search_menu, search, save, reload);
                vb.widget(body);
                vb.widget(status);
                vb.context_menu(editor_menu);
                vb.dialog(discard);
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
        body.set({.value = *text, .placeholder = body.get().placeholder, .rows = body.get().rows});
        refresh_status();
    }

    void confirm_reload() {
        if (body.get().value == saved) return open(current);
        pending_reload = current;
        auto d = discard.get();
        d.open = true;
        d.chosen.reset();
        discard.set(d);
    }

    void write() {
        if (current.empty()) return;
        if (auto ok = notes::save_text(current, body.get().value); !ok) error = "save failed: " + ok.error();
        else { saved = body.get().value; error.clear(); }
        refresh_status();
    }

    // Highlights every match of the search text in the editor (stored in the field's value, so the
    // TextArea draws them), then refreshes the status line. Setting the value notifies again;
    // the second pass finds identical highlights and stops.
    void update_matches() {
        const auto& query = search.get().value;
        std::vector<ui::TextRange> hits;
        for (auto pos : notes::find_all(body.get().value, query)) hits.push_back({pos, pos + query.size()});
        if (hits != body.get().highlights) {
            auto b = body.get();
            b.highlights = std::move(hits);
            body.set(b);
        }
        refresh_status();
    }

    void refresh_status() {
        const auto& text = body.get().value;
        auto query = search.get().value;
        std::optional<std::size_t> matches;
        if (!query.empty()) matches = notes::find_all(text, query).size();
        auto line = notes::status_line(current.filename().string(), notes::stats(text), text != saved, matches);
        status.set({error.empty() ? line : error});
    }
};

int main(int argc, char* argv[]) {
    if (argc >= 2) {   // README capture: open a known file
        NotesApp app{argc >= 3 ? fs::path(argv[2]) : fs::current_path()};
        if (argc >= 4) app.open(argv[3]);
        app.search.set({.value = "include", .placeholder = "Find in file..."});   // show highlights
        if (app.files.rows.size() > 0) app.files.on_row_clicked(0, app.files.rows[0]);
        return showcase(argc, argv, app, 1000, 640);
    }
    NotesApp app{argc >= 2 ? fs::path(argv[1]) : fs::current_path()};
    app::model_app({.title = "PRISM Notes", .width = 1000, .height = 640,
                      .decoration = app::DecorationMode::Custom},
                     app);
}
