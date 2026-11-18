#pragma once

#include <prism/core/field.hpp>
#include <prism/core/types.hpp>
#include <prism/nodes/board_view.hpp>
#include <prism/nodes/port.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace prism::app { class ViewBuilder; }

namespace prism::nodes {

using NodeId = std::uint32_t;
using EdgeId = std::uint32_t;

struct PortRef {
    NodeId node;
    std::size_t port; // index into the node's ports() list
    bool operator==(const PortRef&) const = default;
};

template <class N>
concept NodeType = requires(N& n) {
    { n.ports() } -> std::convertible_to<std::vector<Port*>>;
};

struct NodeOptions {
    std::string title;
    std::string family;
    core::Point at{};
    bool removable = false;   // Delete on the board removes it only when the app allows that
};

enum class ConnectError { NotOutputToInput, TypeMismatch, Cycle, UnknownPort };

struct EdgeInfo { PortRef from, to; };

// The model of a node editor: nodes the app owns (added by reference, never owned here), nodes
// the graph owns (emplaced: destroyed on remove, once their wires are gone, and with the graph),
// and the typed wires between their ports, which the graph owns. An app-owned node's ports must
// outlive the graph or be removed from it first; the graph's destructor touches none of them.
class Graph {
public:
    Graph();
    ~Graph();
    Graph(const Graph&) = delete;
    Graph& operator=(const Graph&) = delete;

    template <NodeType N>
    NodeId add(N& node, NodeOptions opt) {
        return add_entry({node.ports(), view_of_node(node), std::move(opt)});
    }

    // Makes the node in place, a node the graph owns (non-movable is fine): `removable`, as the
    // options say. Returns its id.
    template <NodeType N, class... Args>
    NodeId emplace(NodeOptions opt, Args&&... args) {
        const auto node = std::make_shared<N>(std::forward<Args>(args)...);
        return add_entry({node->ports(), view_of_node(*node), std::move(opt), node});
    }

    void remove(NodeId);

    std::expected<EdgeId, ConnectError> connect(PortRef a, PortRef b);
    bool can_connect(PortRef a, PortRef b) const;
    void disconnect(EdgeId);

    std::optional<EdgeId> edge_into(PortRef input) const;
    const EdgeInfo* edge(EdgeId) const;
    std::vector<EdgeId> edges() const;
    std::vector<NodeId> nodes() const;

    Port* port(PortRef) const;
    const NodeOptions& options(NodeId) const;
    const std::function<void(app::ViewBuilder&)>& view(NodeId) const;
    void move(NodeId, core::Point at);
    core::Point position(NodeId) const;
    // Shifts the whole board's view of the nodes, in board units; their positions stay as they are.
    void pan_by(core::DX, core::DY);
    // The view is a field the app may observe (a Derived reading view() follows it) and save,
    // like a dock layout; set_view restores it, its zoom clamped.
    BoardView view() const { return view_.get(); }
    const core::Field<BoardView>& view_field() const { return view_; }
    void set_view(BoardView);
    // Asks the board to show every card at its next layout, which calls fit() once it knows its size.
    void request_fit();
    bool fit_requested() const { return fit_requested_; }
    // Shows every card (natural size, at its graph position) in a board of `board`'s size.
    void fit(std::span<const core::Rect> cards, core::Size board);
    // Asks the board to lay the nodes out in columns (tidy_layout) at its next layout, and to glide
    // them there; take_tidy_request() says so once, to the board that does it.
    void request_tidy();
    bool tidy_requested() const { return tidy_requested_; }
    bool take_tidy_request() { return std::exchange(tidy_requested_, false); }
    // Asks the board to zoom to `zoom` (clamped) about its centre at its next layout, which calls
    // zoom_centred() once it knows its size.
    void request_zoom(float zoom);
    std::optional<float> zoom_requested() const { return zoom_requested_; }
    // Zooms as asked about the centre of a board of `board`'s size.
    void zoom_centred(core::Size board);

    core::Field<std::uint64_t> structure{0};
    core::Field<std::uint64_t> layout{0};

private:
    struct Edge;
    struct Entry {
        std::vector<Port*> ports;
        std::function<void(app::ViewBuilder&)> view;
        NodeOptions options;
        std::shared_ptr<void> owned{};   // the node, when the graph owns it
    };

    template <class N>
    static std::function<void(app::ViewBuilder&)> view_of_node(N& node) {
        if constexpr (requires(N& n, app::ViewBuilder& vb) { n.view(vb); })
            return [&node](app::ViewBuilder& vb) { node.view(vb); };
        else
            return {};
    }

    NodeId add_entry(Entry entry);
    bool drop_edge(EdgeId, bool revert);
    std::optional<ConnectError> refusal(PortRef from, PortRef to) const;

    std::map<NodeId, Entry> nodes_;
    std::map<EdgeId, std::unique_ptr<Edge>> edges_;
    core::Field<BoardView> view_{};
    bool fit_requested_ = false;
    bool tidy_requested_ = false;
    std::optional<float> zoom_requested_;
    NodeId next_node_ = 1;
    EdgeId next_edge_ = 1;
};

} // namespace prism::nodes
