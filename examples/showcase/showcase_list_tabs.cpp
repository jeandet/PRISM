#include "showcase_common.hpp"

using namespace prism;

struct OpenFiles {
    core::List<std::string> files;
    core::Field<ui::TabBar<>> shown;
    core::Field<ui::Label<>> page{{"The shown file's editor goes here."}};

    OpenFiles() {
        for (auto f : {"main.cpp", "app.hpp", "README.md"}) files.push_back(f);
    }

    void view(app::ViewBuilder& vb) {
        vb.tabs(shown, files, [](const std::string& f) { return f; },
                [&](app::ViewBuilder& p, const std::string&) { p.widget(page); })
            .closable()
            .reorderable();
    }
};

int main(int argc, char* argv[]) {
    OpenFiles model;
    return showcase(argc, argv, model, 420, 110);
}
