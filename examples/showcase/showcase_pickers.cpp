#include "showcase_common.hpp"

#include <chrono>

using namespace prism;

enum class Shape { Circle, Square, Triangle };

struct Pickers {
    core::Field<Shape> shape{Shape::Square};
    core::Field<ui::Choice> port{{.options = {"COM1", "COM3"}, .placeholder = "Pick a port"}};
    core::Field<core::Color> ink{core::Color::rgba(0x33, 0x66, 0xCC)};
    core::Field<std::chrono::year_month_day> day{std::chrono::year{2026} / std::chrono::November / 14};
    core::Field<ui::TimeOfDay> alarm{{7, 30}};

    void view(app::ViewBuilder& vb) { vb.vstack(shape, port, ink, day, alarm); }
};

int main(int argc, char* argv[]) {
    Pickers model;
    return showcase(argc, argv, model, 320, 220);
}
