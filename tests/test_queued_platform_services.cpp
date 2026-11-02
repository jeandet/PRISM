#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/backends/queued_platform_services.hpp>

#include <thread>

using namespace prism::app;
using prism::backends::QueuedPlatformServices;

TEST_CASE("requests from any thread queue up, wake the owning thread, and drain in order") {
    int wakes = 0;
    QueuedPlatformServices q{[&] { ++wakes; }};
    std::thread t([&] {
        q.show_file_dialog(7, {.kind = FileDialogKind::Save, .title = "Save", .filters = {}, .start = {}},
                           [](FileDialogResult) {});
    });
    t.join();
    q.show_message(7, MessageKind::Warning, "Careful", "text");
    CHECK(wakes == 2);
    std::vector<std::string> seen;
    q.drain([&](QueuedPlatformServices::FileRequest& f) { seen.push_back("file:" + f.dialog.title); },
            [&](QueuedPlatformServices::MessageRequest& m) { seen.push_back("message:" + m.title); });
    CHECK(seen == std::vector<std::string>{"file:Save", "message:Careful"});
    std::size_t again = 0;
    q.drain([&](auto&) { ++again; }, [&](auto&) { ++again; });
    CHECK(again == 0);
}
