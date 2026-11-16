#pragma once

#include <cstddef>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

// A sequence of items in a B+ tree whose nodes keep totals: a count and a Summary of everything
// below them. Index access, inserting or erasing in the middle, the total before an index, and
// finding where a running total crosses a value are O(log n). Zed's text buffer is built on the
// same idea (https://zed.dev/blog/zed-decoded-rope-sumtree); PRISM uses it for a text document's
// blocks (bytes before a block) and for a text view's height map (the block at a y).
//
//   struct Sum { std::size_t count; double height; ... operator+ };
//   SumTree<Line, Sum, SummarizeLine> lines;
//   lines.seek([](const Sum& s) { return s.height; }, y);   // the line at y
//
// Summary: default-constructs to the empty total, and adds with operator+. Summarize: a function
// object from an item to its Summary. seek() needs its key never to decrease as items are added.
//
// simplify: nodes are owned, not shared, so a copy is not cheap and there is no snapshot. Zed's
// tree is copy-on-write (shared nodes), which a reader on another thread would need; switch the
// unique_ptr to shared_ptr with copy-before-write when one does.
namespace prism::core {

template <typename Item, typename Summary, typename Summarize>
class SumTree {
public:
    SumTree() = default;
    explicit SumTree(Summarize summarize) : summarize_(std::move(summarize)) {}
    SumTree(SumTree&&) noexcept = default;
    SumTree& operator=(SumTree&&) noexcept = default;

    [[nodiscard]] std::size_t size() const { return root_->count; }
    [[nodiscard]] bool empty() const { return size() == 0; }
    [[nodiscard]] Summary total() const { return root_->sum; }

    [[nodiscard]] const Item& operator[](std::size_t i) const {
        const Node* n = root_.get();
        while (!n->leaf) {
            std::size_t c = 0;
            while (i >= n->children[c]->count) i -= n->children[c++]->count;
            n = n->children[c].get();
        }
        return n->items[i];
    }

    // The total of items [0, i).
    [[nodiscard]] Summary prefix(std::size_t i) const {
        Summary acc{};
        const Node* n = root_.get();
        while (!n->leaf) {
            std::size_t c = 0;
            for (; i >= n->children[c]->count && c + 1 < n->children.size(); ++c) {
                i -= n->children[c]->count;
                acc = acc + n->children[c]->sum;
            }
            n = n->children[c].get();
        }
        for (std::size_t k = 0; k < i && k < n->items.size(); ++k) acc = acc + summarize_(n->items[k]);
        return acc;
    }

    struct Seek {
        std::size_t index;   // the first item whose running total passes the target; size() if none
        Summary before;      // the total of the items before it
    };

    // The first item at which key(running total including it) > target.
    template <typename Key, typename Target>
    [[nodiscard]] Seek seek(Key key, const Target& target) const {
        Summary acc{};
        std::size_t index = 0;
        const Node* n = root_.get();
        while (!n->leaf) {
            const Node* next = nullptr;
            for (const auto& child : n->children) {
                if (key(acc + child->sum) > target) { next = child.get(); break; }
                acc = acc + child->sum;
                index += child->count;
            }
            if (!next) return {index, acc};
            n = next;
        }
        for (const Item& item : n->items) {
            const Summary with = acc + summarize_(item);
            if (key(with) > target) return {index, acc};
            acc = with;
            ++index;
        }
        return {index, acc};
    }

    void insert(std::size_t at, Item item) {
        if (auto sibling = insert_into(*root_, at, std::move(item))) grow_root(std::move(sibling));
    }

    template <typename It>
    void insert(std::size_t at, It first, It last) {
        if (empty() && at == 0) return build(first, last);
        for (; first != last; ++first) insert(at++, *first);
    }

    void erase(std::size_t first, std::size_t last) {
        if (first == 0 && last >= size()) return clear();
        for (std::size_t k = first; k < last; ++k) {
            erase_from(*root_, first);
            while (!root_->leaf && root_->children.size() == 1) root_ = std::move(root_->children.front());
        }
    }

    void clear() { root_ = std::make_unique<Node>(); }

    // Changes item i in place and updates the totals above it.
    template <typename F>
    void modify(std::size_t i, F&& change) {
        modify_in(*root_, i, change);
    }

private:
    static constexpr std::size_t kMax = 32;       // items in a leaf, children in an inner node
    static constexpr std::size_t kMin = kMax / 4; // below this, a node merges with a neighbour

    struct Node {
        bool leaf = true;
        std::size_t count = 0;
        Summary sum{};
        std::vector<Item> items;                      // a leaf's
        std::vector<std::unique_ptr<Node>> children;  // an inner node's
    };

    std::unique_ptr<Node> root_ = std::make_unique<Node>();
    [[no_unique_address]] Summarize summarize_{};

    void recount(Node& n) const {
        n.sum = Summary{};
        if (n.leaf) {
            n.count = n.items.size();
            for (const Item& item : n.items) n.sum = n.sum + summarize_(item);
        } else {
            n.count = 0;
            for (const auto& c : n.children) { n.count += c->count; n.sum = n.sum + c->sum; }
        }
    }

    static std::size_t width(const Node& n) { return n.leaf ? n.items.size() : n.children.size(); }

    // Moves the upper half of an overfull node into a new sibling.
    std::unique_ptr<Node> split(Node& n) const {
        auto right = std::make_unique<Node>();
        right->leaf = n.leaf;
        const std::size_t half = width(n) / 2;
        if (n.leaf) {
            right->items.assign(std::make_move_iterator(n.items.begin() + static_cast<std::ptrdiff_t>(half)),
                                std::make_move_iterator(n.items.end()));
            n.items.resize(half);
        } else {
            right->children.assign(std::make_move_iterator(n.children.begin() + static_cast<std::ptrdiff_t>(half)),
                                   std::make_move_iterator(n.children.end()));
            n.children.resize(half);
        }
        recount(n);
        recount(*right);
        return right;
    }

    void grow_root(std::unique_ptr<Node> sibling) {
        auto root = std::make_unique<Node>();
        root->leaf = false;
        root->children.push_back(std::move(root_));
        root->children.push_back(std::move(sibling));
        recount(*root);
        root_ = std::move(root);
    }

    // Inserts at index `at` below `n`; returns the new right sibling when `n` had to split.
    std::unique_ptr<Node> insert_into(Node& n, std::size_t at, Item&& item) {
        if (n.leaf) {
            n.items.insert(n.items.begin() + static_cast<std::ptrdiff_t>(at), std::move(item));
        } else {
            std::size_t c = 0;
            while (c + 1 < n.children.size() && at > n.children[c]->count) at -= n.children[c++]->count;
            if (auto sibling = insert_into(*n.children[c], at, std::move(item)))
                n.children.insert(n.children.begin() + static_cast<std::ptrdiff_t>(c + 1), std::move(sibling));
        }
        recount(n);
        return width(n) > kMax ? split(n) : nullptr;
    }

    void erase_from(Node& n, std::size_t at) {
        if (n.leaf) {
            n.items.erase(n.items.begin() + static_cast<std::ptrdiff_t>(at));
        } else {
            std::size_t c = 0;
            while (at >= n.children[c]->count) at -= n.children[c++]->count;
            erase_from(*n.children[c], at);
            if (n.children[c]->count == 0) n.children.erase(n.children.begin() + static_cast<std::ptrdiff_t>(c));
            else if (width(*n.children[c]) < kMin) merge_with_neighbour(n, c);
        }
        recount(n);
    }

    // Folds child c into a neighbour when both fit in one node: no underfull leaves pile up.
    void merge_with_neighbour(Node& parent, std::size_t c) {
        const std::size_t left = c > 0 ? c - 1 : c;
        if (left + 1 >= parent.children.size()) return;
        Node& a = *parent.children[left];
        Node& b = *parent.children[left + 1];
        if (width(a) + width(b) > kMax) return;
        if (a.leaf) {
            a.items.insert(a.items.end(), std::make_move_iterator(b.items.begin()), std::make_move_iterator(b.items.end()));
        } else {
            a.children.insert(a.children.end(), std::make_move_iterator(b.children.begin()),
                              std::make_move_iterator(b.children.end()));
        }
        recount(a);
        parent.children.erase(parent.children.begin() + static_cast<std::ptrdiff_t>(left + 1));
    }

    template <typename F>
    void modify_in(Node& n, std::size_t i, F& change) {
        if (n.leaf) {
            change(n.items[i]);
        } else {
            std::size_t c = 0;
            while (i >= n.children[c]->count) i -= n.children[c++]->count;
            modify_in(*n.children[c], i, change);
        }
        recount(n);
    }

    // Fills an empty tree level by level: a large document loads in O(n), not O(n log n).
    template <typename It>
    void build(It first, It last) {
        std::vector<std::unique_ptr<Node>> level;
        while (first != last) {
            auto leaf = std::make_unique<Node>();
            for (std::size_t k = 0; k < kMax && first != last; ++k, ++first) leaf->items.push_back(*first);
            recount(*leaf);
            level.push_back(std::move(leaf));
        }
        if (level.empty()) return;
        while (level.size() > 1) {
            std::vector<std::unique_ptr<Node>> up;
            for (std::size_t k = 0; k < level.size(); k += kMax) {
                auto inner = std::make_unique<Node>();
                inner->leaf = false;
                for (std::size_t j = k; j < level.size() && j < k + kMax; ++j) inner->children.push_back(std::move(level[j]));
                recount(*inner);
                up.push_back(std::move(inner));
            }
            level = std::move(up);
        }
        root_ = std::move(level.front());
    }
};

} // namespace prism::core
