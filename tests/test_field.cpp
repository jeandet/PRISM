#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/field.hpp>

#include <string>
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#endif
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}


TEST_CASE("Field stores value") {
    prism::Field<int> f{42};
    CHECK(f.get() == 42);
}

TEST_CASE("Field implicit conversion to const T&") {
    prism::Field<std::string> f{"hello"};
    const std::string& ref = f;
    CHECK(ref == "hello");
}

TEST_CASE("Field::set updates value") {
    prism::Field<int> f{0};
    f.set(10);
    CHECK(f.get() == 10);
}

TEST_CASE("Field::set notifies on_change receivers") {
    prism::Field<int> f{0};
    int received = -1;
    auto conn = f.on_change().connect([&](const int& v) { received = v; });
    f.set(7);
    CHECK(received == 7);
}

TEST_CASE("Field::set does not notify if value unchanged") {
    prism::Field<int> f{5};
    int calls = 0;
    auto conn = f.on_change().connect([&](const int&) { ++calls; });
    f.set(5);
    CHECK(calls == 0);
    f.set(6);
    CHECK(calls == 1);
}

TEST_CASE("Field multiple receivers") {
    prism::Field<std::string> f{"a"};
    std::string r1, r2;
    auto c1 = f.on_change().connect([&](const std::string& v) { r1 = v; });
    auto c2 = f.on_change().connect([&](const std::string& v) { r2 = v; });
    f.set("b");
    CHECK(r1 == "b");
    CHECK(r2 == "b");
}

TEST_CASE("Field connection RAII disconnect") {
    prism::Field<int> f{0};
    int received = 0;
    {
        auto conn = f.on_change().connect([&](const int& v) { received = v; });
        f.set(1);
        CHECK(received == 1);
    }
    f.set(2);
    CHECK(received == 1);
}

TEST_CASE("Field default-constructed value") {
    prism::Field<int> f{};
    CHECK(f.get() == 0);
}

TEST_CASE("Field::observe callback lives as long as field") {
    prism::Field<int> f{0};
    int received = 0;
    f.observe([&](const int& v) { received = v; });
    f.set(42);
    CHECK(received == 42);
    f.set(99);
    CHECK(received == 99);
}

TEST_CASE("Field::observe multiple observers") {
    prism::Field<int> f{0};
    int r1 = 0, r2 = 0;
    f.observe([&](const int& v) { r1 = v; });
    f.observe([&](const int& v) { r2 = v; });
    f.set(5);
    CHECK(r1 == 5);
    CHECK(r2 == 5);
}

TEST_CASE("Field pipe: on_change() | prism::then(f)") {
    prism::Field<int> f{0};
    int received = -1;
    auto conn = f.on_change() | prism::then([&](const int& v) { received = v; });
    f.set(42);
    CHECK(received == 42);
}

TEST_CASE("Field with bool") {
    prism::Field<bool> f{false};
    bool toggled = false;
    auto conn = f.on_change().connect([&](const bool& v) { toggled = v; });
    f.set(true);
    CHECK(toggled == true);
    CHECK(f.get() == true);
}

TEST_CASE("Field: = and the compound operators set the value and notify once") {
    prism::Field<int> f{1};
    int calls = 0;
    auto conn = f.on_change().connect([&](const int&) { ++calls; });
    f = 5;
    CHECK(f.get() == 5);
    f += 3;
    CHECK(f.get() == 8);
    f -= 2;
    f *= 4;
    f /= 3;
    CHECK(f.get() == 8);
    f %= 5;
    CHECK(f.get() == 3);
    f |= 4;
    f &= 6;
    f ^= 1;
    CHECK(f.get() == 7);
    CHECK(calls == 9);
    f = 7;   // equal: no notification, as set() does
    f += 0;
    CHECK(calls == 9);
}

TEST_CASE("Field: ++ and --") {
    prism::Field<int> f{1};
    int calls = 0;
    auto conn = f.on_change().connect([&](const int&) { ++calls; });
    ++f;
    CHECK(f.get() == 2);
    CHECK(f++ == 2);
    CHECK(f.get() == 3);
    --f;
    CHECK(f-- == 2);
    CHECK(f.get() == 1);
    CHECK(calls == 4);
}

TEST_CASE("Field<std::string>: += appends, == and < compare with a literal") {
    prism::Field<std::string> name{"ada"};
    name += " lovelace";
    CHECK(name == "ada lovelace");
    CHECK("ada lovelace" == name);
    CHECK(name != "babbage");
    CHECK(name < "b");
    name = "grace";
    CHECK(name.get() == "grace");
}

namespace {
struct Progress {
    double value = 0;
    bool busy = false;
    bool operator==(const Progress&) const = default;
};
} // namespace

TEST_CASE("Field: = takes a designated initializer") {
    prism::Field<Progress> p;
    p = {.value = 0.5, .busy = true};
    CHECK(p.get().value == 0.5);
    CHECK(p.get().busy);
}

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
// POSIX only: the child is a fork, so its death can be inspected with waitpid. The guard must
// hold whether or not NDEBUG is defined — today it is only an assert, so a release build lets
// the set() through and the child reaches _exit(0).
TEST_CASE("Field::set from a thread that is not the logic thread aborts, with or without NDEBUG") {
    // A joined thread's id: never this thread's, so the child below looks like "another thread".
    std::thread::id other;
    std::thread([&other] { other = std::this_thread::get_id(); }).join();

    const pid_t pid = fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        // Pretend an app runs and its logic thread is `other`: this thread may not set fields.
        prism::core::logic_thread.store(other);
        prism::Field<int> f{0};
        f.set(1);   // must print the violation and abort, assert or not
        _exit(0);   // reached only when the check is compiled out (NDEBUG today)
    }
    int status = 0;
    REQUIRE(waitpid(pid, &status, 0) == pid);
    CHECK(WIFSIGNALED(status));
    CHECK(WTERMSIG(status) == SIGABRT);
}
#endif
