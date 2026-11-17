#pragma once

#include <prism/core/field.hpp>
#include <prism/core/types.hpp>
#include <prism/nodes/port.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
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

// The model of a node editor: nodes the app owns (referenced here, never owned) and the typed
// wires between their ports, which the graph owns. Ports must outlive the graph or be removed
// from it first; the graph's own destructor touches no port.
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
    // Shifts the whole board's view of the nodes; their positions stay as they are.
    void pan_by(core::DX, core::DY);
    core::Point pan() const { return pan_; }

    core::Field<std::uint64_t> structure{0};
    core::Field<std::uint64_t> layout{0};

private:
    struct Edge;
    struct Entry {
        std::vector<Port*> ports;
        std::function<void(app::ViewBuilder&)> view;
        NodeOptions options;
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
    core::Point pan_{};
    NodeId next_node_ = 1;
    EdgeId next_edge_ = 1;
};

} // namespace prism::nodes
