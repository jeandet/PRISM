#include <prism/nodes/graph.hpp>

#include <algorithm>
#include <set>
#include <utility>

namespace prism::nodes {

// simplify: values cross as std::any copies, synchronously; the Edge is the seam where a
// later version hands them to another thread (spec goal 11). The callback captures raw port
// pointers: a receiver removing the downstream node during the same emit would reach a freed
// port; that needs a liveness flag once board callbacks change the graph.
struct Graph::Edge {
    EdgeInfo info;
    core::Connection link;
};

Graph::Graph() = default;

Graph::~Graph() {
    for (auto& [id, e] : edges_) e->link.disconnect();
    edges_.clear();
    nodes_.clear();
}

NodeId Graph::add_entry(Entry entry) {
    const NodeId id = next_node_++;
    nodes_.emplace(id, std::move(entry));
    structure.set(structure.get() + 1);
    return id;
}

Port* Graph::port(PortRef r) const {
    const auto it = nodes_.find(r.node);
    if (it == nodes_.end() || r.port >= it->second.ports.size()) return nullptr;
    return it->second.ports[r.port];
}

std::optional<ConnectError> Graph::refusal(PortRef from, PortRef to) const {
    const Port* f = port(from);
    const Port* t = port(to);
    if (!f || !t) return ConnectError::UnknownPort;
    if (f->dir() != Dir::Out || t->dir() != Dir::In) return ConnectError::NotOutputToInput;
    if (f->type() != t->type()) return ConnectError::TypeMismatch;
    std::set<NodeId> visited;
    const auto reaches = [this, &visited](auto&& self, NodeId at, NodeId target) -> bool {
        if (at == target) return true;
        if (!visited.insert(at).second) return false;
        return std::ranges::any_of(edges_, [&](const auto& kv) {
            const EdgeInfo& e = kv.second->info;
            return e.from.node == at && self(self, e.to.node, target);
        });
    };
    if (reaches(reaches, to.node, from.node)) return ConnectError::Cycle;
    return std::nullopt;
}

namespace nodes_detail {

std::pair<PortRef, PortRef> output_first(const Graph& g, PortRef a, PortRef b) {
    const Port* pa = g.port(a);
    return pa && pa->dir() == Dir::In ? std::pair{b, a} : std::pair{a, b};
}

} // namespace nodes_detail

bool Graph::can_connect(PortRef a, PortRef b) const {
    const auto [from, to] = nodes_detail::output_first(*this, a, b);
    return !refusal(from, to);
}

std::expected<EdgeId, ConnectError> Graph::connect(PortRef a, PortRef b) {
    const auto [from, to] = nodes_detail::output_first(*this, a, b);
    if (const auto why = refusal(from, to)) return std::unexpected(*why);
    if (const auto old = edge_into(to)) drop_edge(*old, false);
    Port* out = port(from);
    Port* in = port(to);
    in->set_value(out->value());
    const EdgeId id = next_edge_++;
    edges_.emplace(id, std::make_unique<Edge>(Edge{
        {from, to}, out->on_change([out, in] { in->set_value(out->value()); })}));
    structure.set(structure.get() + 1);
    return id;
}

bool Graph::drop_edge(EdgeId id, bool revert) {
    const auto it = edges_.find(id);
    if (it == edges_.end()) return false;
    const PortRef to = it->second->info.to;
    edges_.erase(it);
    if (revert) port(to)->reset();
    return true;
}

void Graph::disconnect(EdgeId id) {
    if (drop_edge(id, true)) structure.set(structure.get() + 1);
}

void Graph::remove(NodeId id) {
    for (const EdgeId e : edges()) {
        const EdgeInfo& info = edges_.at(e)->info;
        if (info.from.node == id || info.to.node == id) drop_edge(e, true);
    }
    if (nodes_.erase(id)) structure.set(structure.get() + 1);
}

std::optional<EdgeId> Graph::edge_into(PortRef input) const {
    const auto it = std::ranges::find_if(edges_, [&](const auto& kv) { return kv.second->info.to == input; });
    return it == edges_.end() ? std::nullopt : std::optional{it->first};
}

const EdgeInfo* Graph::edge(EdgeId id) const {
    const auto it = edges_.find(id);
    return it == edges_.end() ? nullptr : &it->second->info;
}

std::vector<EdgeId> Graph::edges() const {
    std::vector<EdgeId> out;
    for (const auto& [id, e] : edges_) out.push_back(id);
    return out;
}

std::vector<NodeId> Graph::nodes() const {
    std::vector<NodeId> out;
    for (const auto& [id, n] : nodes_) out.push_back(id);
    return out;
}

const NodeOptions& Graph::options(NodeId id) const { return nodes_.at(id).options; }

const std::function<void(app::ViewBuilder&)>& Graph::view(NodeId id) const { return nodes_.at(id).view; }

void Graph::move(NodeId id, core::Point at) {
    nodes_.at(id).options.at = at;
    layout.set(layout.get() + 1);
}

core::Point Graph::position(NodeId id) const { return nodes_.at(id).options.at; }

void Graph::pan_by(core::DX dx, core::DY dy) {
    if (dx.raw() == 0.f && dy.raw() == 0.f) return;
    set_view({view().pan + core::Offset{dx, dy}, view().zoom});
}

void Graph::set_view(BoardView v) {
    v.zoom = clamp_zoom(v.zoom);
    if (v == view()) return;
    view_.set(v);
    layout.set(layout.get() + 1);
}

void Graph::request_fit() {
    fit_requested_ = true;
    layout.set(layout.get() + 1);
}

void Graph::request_tidy() {
    tidy_requested_ = true;
    layout.set(layout.get() + 1);
}

void Graph::fit(std::span<const core::Rect> cards, core::Size board) {
    fit_requested_ = false;
    set_view(fit_all(cards, board));
}

void Graph::request_zoom(float zoom) {
    zoom_requested_ = zoom;
    layout.set(layout.get() + 1);
}

void Graph::zoom_centred(core::Size board) {
    if (!zoom_requested_) return;
    const float zoom = *std::exchange(zoom_requested_, std::nullopt);
    set_view(zoomed_about(view(), core::Offset{core::DX{board.w.raw() / 2}, core::DY{board.h.raw() / 2}}, zoom));
}

} // namespace prism::nodes
