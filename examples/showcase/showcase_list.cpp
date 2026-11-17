#include "showcase_common.hpp"

#include <optional>

using namespace prism;

struct Groceries {
    core::List<std::string> items;
    core::Field<std::optional<std::size_t>> picked{std::size_t{1}};

    Groceries() {
        for (auto s : {"Milk", "Eggs", "Flour", "Sugar"}) items.push_back(s);
    }

    void view(app::ViewBuilder& vb) { vb.list(items).selection(picked).reorderable(); }
};

int main(int argc, char* argv[]) {
    Groceries model;
    return showcase(argc, argv, model, 260, 150);
}
