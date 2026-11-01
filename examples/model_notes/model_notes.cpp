// Notes: a small text editor -- a directory tree, a file editor, search, save. The text-heavy
// reference app: it exercises editing, selection, clipboard, undo, IME and text measurement the
// way a dashboard never does. Logic lives in notes_logic.hpp (tested); this file only wires it.
#include <prism/prism.hpp>
#include "../model_tree_browser/file_tree_source.hpp"
#include "../showcase/showcase_common.hpp"
#include "notes_logic.hpp"

#include <filesystem>
#include <vector>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

namespace fs = std::filesystem;

struct NotesApp {
    FileTreeSource source;
    prism::TreeController files{prism::wrap_tree_storage(source)};
    prism::Field<prism::TextField<>> search{{.placeholder = "Find in file..."}};
    prism::Field<prism::Button> save{{"Save"}};
    prism::Field<prism::Button> reload{{"Reload"}};
    prism::Field<prism::TextArea<>> body{{.placeholder = "Select a text file on the left", .rows = 26}};
    prism::Field<prism::Label<>> status{{"No file open"}};

    fs::path current;
    std::string saved;   // the file as last loaded or saved: tells whether `body` was modified
    std::string error;   // last load/save problem, shown in the status line
    std::vector<prism::Connection> wiring;   // captures `this`: NotesApp is never moved

    explicit NotesApp(fs::path root) : source(std::move(root)) {
        wiring.push_back(files.selected.on_change().connect([this](const auto& id) {
            std::error_code ec;
            if (id && fs::is_regular_file(source.path_of(*id), ec)) open(source.path_of(*id));
        }));
        wiring.push_back(body.on_change().connect([this](const auto&) { refresh_status(); }));
        wiring.push_back(search.on_change().connect([this](const auto&) { refresh_status(); }));
        wiring.push_back(save.on_change().connect([this](const auto&) { write(); }));
        wiring.push_back(reload.on_change().connect([this](const auto&) { if (!current.empty()) open(current); }));
    }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.hstack([&] {
            vb.tree(files);
            vb.handle();
            vb.vstack([&] {
                vb.hstack(search, save, reload);
                vb.widget(body);
                vb.widget(status);
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

    void write() {
        if (current.empty()) return;
        if (auto ok = notes::save_text(current, body.get().value); !ok) error = "save failed: " + ok.error();
        else { saved = body.get().value; error.clear(); }
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
        if (app.files.rows.size() > 0) app.files.on_row_clicked(0, app.files.rows[0]);
        return showcase(argc, argv, app, 1000, 640);
    }
    NotesApp app{argc >= 2 ? fs::path(argv[1]) : fs::current_path()};
    prism::model_app({.title = "PRISM Notes", .width = 1000, .height = 640,
                      .decoration = prism::DecorationMode::Custom},
                     app);
}
