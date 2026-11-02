#pragma once

#include <prism/prism.hpp>
#include <prism/app/capturing_backend.hpp>
#include <prism/render/svg_export.hpp>

#include <fstream>
#include <functional>
#include <iostream>
#include <string>

// Run model_app headlessly, capture snapshot, write SVG to argv[1]. `setup` runs exactly
// like a real model_app setup lambda would, so an example can seed any state it wires up
// there (e.g. a computed label) -- as long as that state doesn't depend on the animation
// clock ticking, since a headless capture never runs a real tick loop.
template <typename Model>
int showcase(int argc, char* argv[], Model& model, int w, int h,
             std::function<void(prism::app::AppContext&)> setup) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <output.svg>\n";
        return 1;
    }

    std::shared_ptr<const prism::render::SceneSnapshot> snap;
    auto backend = prism::app::Backend{std::make_unique<prism::app::CapturingBackend>(snap)};
    auto& window = backend.create_window({.title = "showcase", .width = w, .height = h});
    prism::app::model_app(backend, window, model, setup);

    if (!snap) {
        std::cerr << "No snapshot captured\n";
        return 1;
    }

    std::ofstream out(argv[1]);
    out << prism::render::to_svg(*snap, static_cast<float>(w), static_cast<float>(h));
    return 0;
}

template <typename Model>
int showcase(int argc, char* argv[], Model& model, int w = 400, int h = 300) {
    return showcase(argc, argv, model, w, h, std::function<void(prism::app::AppContext&)>{});
}
