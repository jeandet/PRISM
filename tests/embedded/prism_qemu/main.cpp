// PRISM's core in QEMU (mps2-an500, Cortex-M7): build a small model with a
// hand-written view() (the toolchain has no reflection), construct a
// WidgetTree on it, publish SceneSnapshots, and stress the publish path.
// No app runs, so any thread may set fields (core::may_change_model()); this
// task is the one that does — the worker thread below only builds snapshots.
//
// Output through printf (per-task newlib reent + ports/freertos/newlib_locks.c);
// semihosting qemu_log() for the banner and the watchdog. Exit code = failed checks.
#include <prism/app/widget_tree.hpp>
#include <prism/core/field.hpp>
#include <prism/render/scene_snapshot.hpp>

#include <prism/raster/canvas.hpp>
#include <prism/raster/draw_snapshot.hpp>
#include <prism/raster/fonts.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <variant>

#include "FreeRTOS.h"
#include "task.h"

extern "C" {
#include "qemu_log.h"
}

using prism::app::ViewBuilder;
using prism::app::WidgetTree;
using prism::core::Field;
using prism::render::SceneSnapshot;
using prism::render::TextCmd;

namespace {

constexpr const char *kLabelText = "PRISM on QEMU";

// The model: a string (drawn as text), a counter, and a label, button,
// checkbox and slider. view() is hand-written — no reflection on this target.
struct DemoModel {
    Field<std::string> title{kLabelText};
    Field<int> counter{0};
    Field<prism::ui::Label<>> heading{{.value = "counter demo"}};
    Field<prism::ui::Button> run{{"Run"}};
    Field<bool> enabled{true};
    Field<prism::ui::Slider<>> volume{{.value = 0.5}};

    void view(ViewBuilder &vb) {
        vb.vstack(title, counter, heading, run, enabled, volume);
    }
};

int failures = 0;

void check(const char *name, bool ok) {
    std::printf("%-52s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

// Whether any draw list of the snapshot carries a TextCmd with exactly `text`.
bool shows_text(const SceneSnapshot &snap, std::string_view text) {
    for (const auto &dl : snap.draw_lists)
        for (const auto &cmd : dl->commands)
            if (const auto *t = std::get_if<TextCmd>(&cmd); t && t->text == text) return true;
    return false;
}

std::size_t command_count(const SceneSnapshot &snap) {
    std::size_t n = snap.overlay.size();
    for (const auto &dl : snap.draw_lists) n += dl->size();
    return n;
}

void prism_test_task(void *) {
    std::setvbuf(stdout, nullptr, _IONBF, 0); // one _write per printf: whole lines
    qemu_log("PRISM core tests in QEMU (mps2-an500)\n");

    DemoModel model;
    WidgetTree tree(model);
    prism::raster::Fonts fonts;   // the embedded TTFs, also the tree's measurer: layout and drawing agree
    tree.set_text_measurer(&fonts);

    // 1-3. one snapshot of the model: geometry, draw commands, the label's text.
    uint64_t version = 0;
    auto snap = tree.build_snapshot(480, 272, ++version);
    tree.clear_dirty();
    check("snapshot built", snap != nullptr);
    check("snapshot has geometry", snap && !snap->geometry.empty());
    check("snapshot has draw commands", snap && command_count(*snap) > 0);
    check("a TextCmd carries the label text", snap && shows_text(*snap, kLabelText));
    if (snap)
        std::printf("  geometry: %zu entries, draw commands: %zu, approx bytes: %zu, dirty widgets: %zu\n",
                    snap->geometry.size(), command_count(*snap), snap->approx_bytes,
                    snap->dirty_widget_count);

    // 4. set the counter 100 times, one snapshot per change; the last shows 100.
    constexpr int kSets = 100;
    auto t0 = std::chrono::steady_clock::now();
    std::unique_ptr<SceneSnapshot> last;
    for (int i = 1; i <= kSets; ++i) {
        model.counter.set(i); // the logic thread (this task) is the only one setting fields
        last = tree.build_snapshot(480, 272, ++version);
        tree.clear_dirty();
    }
    auto t1 = std::chrono::steady_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    check("last snapshot shows counter 100", last && shows_text(*last, "100"));
    std::printf("  %d publishes: %.1f ms wall total, %.3f ms each (last build_time_ms %.3f)\n",
                kSets, total_ms, total_ms / kSets, last ? last->build_time_ms : 0.0);

    // 5. rasterize that snapshot into an in-RAM 480x272 framebuffer (on the board the LTDC
    //    framebuffer lives in SDRAM): a checksum of the pixels, and one full draw's time.
    if (last) {
        static std::array<std::uint32_t, 480 * 272> fb;   // 512 KiB of the 4 MiB SRAM
        prism::raster::Canvas canvas{fb, 480, 272, 480};
        auto checksum = [](const std::array<std::uint32_t, 480 * 272>& px) {
            std::uint64_t h = 1469598103934665603ull;   // FNV-1a 64 over the pixel bytes
            for (std::uint32_t v : px)
                for (int b = 0; b < 4; ++b) {
                    h ^= (v >> (8 * b)) & 0xFFull;
                    h *= 1099511628211ull;
                }
            return h;
        };
        canvas.clear(last->window.background);   // clear is the caller's job, as with SDL
        auto d0 = std::chrono::steady_clock::now();
        prism::raster::draw(canvas, *last, fonts);
        auto d1 = std::chrono::steady_clock::now();
        double draw_ms = std::chrono::duration<double, std::milli>(d1 - d0).count();
        std::uint64_t sum = checksum(fb);
        std::set<std::uint32_t> colors(fb.begin(), fb.end());
        canvas.clear(last->window.background);
        prism::raster::draw(canvas, *last, fonts);
        check("rasterized framebuffer is deterministic", checksum(fb) == sum);
        check("framebuffer is not one colour", colors.size() > 1);
        std::printf("  one full draw of %zu commands: %.3f ms, checksum %016llx, %zu colours\n",
                    command_count(*last), draw_ms, static_cast<unsigned long long>(sum),
                    colors.size());
    }

    // 6. a second thread builds snapshots while this task changes nothing (it
    //    only churns malloc, under newlib's locked malloc) — two threads plus
    //    malloc under load.
    constexpr int kWorkerBuilds = 200;
    std::atomic<int> built{0};
    std::thread worker([&] {
        for (int i = 0; i < kWorkerBuilds; ++i) {
            auto s = tree.build_snapshot(480, 272, ++version);
            tree.clear_dirty();
            if (s && !s->geometry.empty() && shows_text(*s, "100")) built.fetch_add(1);
        }
    });
    long churn = 0;
    while (built.load() < kWorkerBuilds) {
        for (int i = 0; i < 64; ++i) {
            std::string s(static_cast<std::size_t>(16 + churn % 497), 'q'); // malloc/free churn
            ++churn;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    worker.join();
    check("2nd thread published while this task churned malloc",
          built.load() == kWorkerBuilds && churn > 0);
    std::printf("  worker built %d snapshots, task churned %ld mallocs meanwhile\n",
                built.load(), churn);

    std::printf("failures: %d\n", failures);
    exit_qemu(failures); // exit code = number of failed checks
}

void watchdog_task(void *) {
    vTaskDelay(pdMS_TO_TICKS(60000));
    qemu_log("WATCHDOG: PRISM tests did not finish in 60 s\n");
    exit_qemu(99);
}

} // namespace

int main() {
    xTaskCreate(prism_test_task, "prism", 16384, nullptr, 2, nullptr); // 64 KiB stack
    xTaskCreate(watchdog_task, "watchdog", 512, nullptr, configMAX_PRIORITIES - 1, nullptr);
    vTaskStartScheduler();
    for (;;) { }
}
