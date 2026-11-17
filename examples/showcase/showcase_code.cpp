#include "showcase_common.hpp"

using namespace prism;

struct CodeShowcase {
    text::Document source{R"(#include <cstdio>

// Says hello.
int main() {
    const char* name = "ada";
    std::printf("Hello, %s!\n", name);
    return 0;
})"};

    void view(app::ViewBuilder& vb) { vb.code_editor(source, "cpp"); }
};

int main(int argc, char* argv[]) {
    CodeShowcase model;
    return showcase(argc, argv, model, 520, 220);
}
