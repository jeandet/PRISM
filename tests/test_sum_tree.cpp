// core::SumTree: a B+ tree of items whose nodes keep totals. Checked against a plain vector doing
// the same edits: same items, same prefix totals, same seek answers, after thousands of random edits.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/sum_tree.hpp>

#include <cstdint>
#include <numeric>
#include <random>
#include <string>
#include <vector>

using prism::core::SumTree;

namespace {
// The total of a run of ints: how many, and their sum.
struct IntSum {
    std::size_t count = 0;
    long long sum = 0;
    friend IntSum operator+(IntSum a, IntSum b) { return {a.count + b.count, a.sum + b.sum}; }
};
struct SumOfInt {
    IntSum operator()(int v) const { return {1, v}; }
};
using Tree = SumTree<int, IntSum, SumOfInt>;

long long prefix_sum(const std::vector<int>& v, std::size_t n) {
    return std::accumulate(v.begin(), v.begin() + static_cast<long>(n), 0LL);
}

void check_same(const Tree& t, const std::vector<int>& ref) {
    REQUIRE(t.size() == ref.size());
    for (std::size_t i = 0; i < ref.size(); ++i) REQUIRE(t[i] == ref[i]);
    CHECK(t.total().sum == prefix_sum(ref, ref.size()));
    for (std::size_t i = 0; i <= ref.size(); i += 1 + ref.size() / 17)
        REQUIRE(t.prefix(i).sum == prefix_sum(ref, i));
}
} // namespace

TEST_CASE("an empty tree, then a few items in order") {
    Tree t;
    CHECK(t.size() == 0);
    CHECK(t.total().sum == 0);
    for (int i = 0; i < 5; ++i) t.insert(t.size(), i + 1);
    check_same(t, {1, 2, 3, 4, 5});
}

TEST_CASE("seek finds the item where a running total crosses a value") {
    Tree t;
    for (int v : {3, 1, 4, 1, 5, 9, 2, 6}) t.insert(t.size(), v);
    // running sums: 3 4 8 9 14 23 25 31
    auto at = t.seek([](const IntSum& s) { return s.sum; }, 8LL);   // 8 is reached inside item 2
    CHECK(at.index == 3);       // the first item whose running total passes 8 (strictly)
    CHECK(at.before.sum == 8);
    auto first = t.seek([](const IntSum& s) { return s.sum; }, 0LL);
    CHECK(first.index == 0);
    auto past = t.seek([](const IntSum& s) { return s.sum; }, 1000LL);
    CHECK(past.index == t.size());
}

TEST_CASE("thousands of random edits agree with a vector") {
    std::mt19937 rng(20261116);
    Tree t;
    std::vector<int> ref;
    for (int step = 0; step < 20000; ++step) {
        const int op = static_cast<int>(rng() % 10);
        if (op < 5 || ref.empty()) {                       // insert
            const std::size_t at = rng() % (ref.size() + 1);
            const int v = static_cast<int>(rng() % 1000);
            t.insert(at, v);
            ref.insert(ref.begin() + static_cast<long>(at), v);
        } else if (op < 8) {                               // erase a run
            const std::size_t at = rng() % ref.size();
            const std::size_t n = std::min<std::size_t>(1 + rng() % 40, ref.size() - at);
            t.erase(at, at + n);
            ref.erase(ref.begin() + static_cast<long>(at), ref.begin() + static_cast<long>(at + n));
        } else {                                           // change one in place
            const std::size_t at = rng() % ref.size();
            const int v = static_cast<int>(rng() % 1000);
            t.modify(at, [&](int& x) { x = v; });
            ref[at] = v;
        }
        if (step % 997 == 0) check_same(t, ref);
    }
    check_same(t, ref);
    for (long long target : {0LL, 1LL, 5000LL, t.total().sum / 2, t.total().sum - 1}) {
        auto at = t.seek([](const IntSum& s) { return s.sum; }, target);
        long long run = 0;
        std::size_t want = 0;
        while (want < ref.size() && run + ref[want] <= target) run += ref[want++];
        CHECK(at.index == want);
        CHECK(at.before.sum == run);
    }
}

TEST_CASE("inserting many at once and erasing everything") {
    Tree t;
    std::vector<int> many(10000);
    std::iota(many.begin(), many.end(), 0);
    t.insert(0, many.begin(), many.end());
    check_same(t, many);
    t.erase(0, t.size());
    CHECK(t.size() == 0);
    t.insert(0, 7);
    check_same(t, {7});
}

TEST_CASE("items that are not trivial: strings") {
    struct Bytes {
        std::size_t operator()(const std::string& s) const { return s.size(); }
    };
    SumTree<std::string, std::size_t, Bytes> lines;
    lines.insert(0, std::string("hello"));
    lines.insert(1, std::string("world!"));
    lines.modify(0, [](std::string& s) { s += ","; });
    CHECK(lines.total() == 12);
    CHECK(lines[0] == "hello,");
}
