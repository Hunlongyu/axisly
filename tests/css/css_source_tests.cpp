#include <axisly/css_source.hpp>

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace axisly;
using namespace axisly::css;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
constexpr std::size_t capacity = 32;
std::size_t index(NodeId id) { return static_cast<std::uint32_t>(id.value) - std::size_t{1}; }
struct Cell {
    NodeId id;
    bool active{};
    Style style;
    std::optional<NodeId> parent;
    std::vector<NodeId> children;
    std::shared_ptr<Unit> width{std::make_shared<Unit>(20)};
};
struct Source final : LayoutSource {
    std::vector<Cell> cells{capacity};
    mutable std::size_t reads{};
    bool fail{};
    std::optional<LayoutNodeView> node(NodeId id) const override {
        if (fail) throw std::runtime_error("Host read failed");
        ++reads;
        const auto i = index(id);
        if (i >= cells.size() || !cells[i].active || cells[i].id != id) return std::nullopt;
        const auto& cell = cells[i];
        return LayoutNodeView{cell.style, cell.children, cell.parent, cell.width.get()};
    }
    void detach(NodeId id) {
        auto& cell = cells[index(id)];
        if (cell.parent) {
            auto& siblings = cells[index(*cell.parent)].children;
            std::erase(siblings, id);
            cell.parent.reset();
        }
    }
    void append(NodeId parent, NodeId child) {
        detach(child);
        cells[index(parent)].children.push_back(child);
        cells[index(child)].parent = parent;
    }
    bool can_parent(NodeId parent, NodeId child) const {
        for (auto id = std::optional<NodeId>{parent}; id; id = cells[index(*id)].parent) {
            if (*id == child) return false;
        }
        return true;
    }
};
struct Provider final : MeasureProvider {
    const Source* source{};
    Measurement measure(NodeId id, const Style& style, void* raw, const MeasureRequest& request) const override {
        if (source) require(&style == &source->cells[index(id)].style, "Host Style was copied");
        const auto width = raw ? *static_cast<Unit*>(raw) : Unit{0};
        return {{request.known_content_width.value_or(width), request.known_content_height.value_or(11)}};
    }
};
void compare(const LayoutResult& actual, const LayoutResult& expected,
             const std::array<NodeId, capacity>& actual_ids,
             const std::array<NodeId, capacity>& expected_ids) {
    require(actual.succeeded() && expected.succeeded(), "Layout failed");
    require(actual.size == expected.size && actual.nodes.size() == expected.nodes.size(), "Layout size/count differs");
    for (std::size_t i = 0; i < capacity; ++i) {
        const auto* a = actual.find(actual_ids[i]);
        const auto* b = expected.find(expected_ids[i]);
        require(bool(a) == bool(b), "Published node set differs");
        if (!a) continue;
        auto normalized = *a;
        normalized.node = b->node;
        if (a->parent) {
            const auto parent = std::find(actual_ids.begin(), actual_ids.end(), *a->parent);
            require(parent != actual_ids.end(), "Unknown parent in publication");
            normalized.parent = expected_ids[static_cast<std::size_t>(parent - actual_ids.begin())];
        }
        require(normalized == *b, "Geometry differs from a newly built tree");
    }
    require(actual.grid_tracks.size() == expected.grid_tracks.size(), "Diagnostic count differs");
    auto reports = actual.grid_tracks;
    const auto remap = [&](NodeId id) {
        const auto item = std::find(actual_ids.begin(), actual_ids.end(), id);
        require(item != actual_ids.end(), "Unknown grid report node");
        return expected_ids[static_cast<std::size_t>(item - actual_ids.begin())];
    };
    for (auto& report : reports) {
        report.node = remap(report.node);
        for (auto& item : report.items) item.node = remap(item.node);
    }
    require(reports == expected.grid_tracks, "Grid diagnostics differ from a newly built tree");
}

void stateful(std::uint32_t seed) {
    Source source;
    Provider provider, borrowed_provider;
    borrowed_provider.source = &source;
    LayoutTree retained;
    LayoutSession session(source);
    std::array<NodeId, capacity> ids{}, host_ids{};
    const auto random = [&]() {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        return seed;
    };
    const auto initialize = [&](std::size_t i) {
        auto& cell = source.cells[i];
        cell.id.value = ((cell.id.value >> 32) + 1) << 32 | (i + 1);
        cell.active = true;
        cell.parent.reset(); cell.children.clear();
        cell.style = {};
        cell.style.display = i % 3 == 0 ? Display::Grid : i % 3 == 1 ? Display::Flex : Display::Block;
        cell.style.grid.template_columns = {TrackSizing::fixed(43), TrackSizing::fixed(61)};
        cell.style.box.padding.left = Length::length(static_cast<Unit>(i % 4) * .25F);
        ids[i] = retained.create_node(cell.style);
        retained.set_context(ids[i], cell.width);
        host_ids[i] = cell.id;
    };
    for (std::size_t i = 0; i < capacity; ++i) {
        initialize(i);
        if (i) {
            const auto parent = (i - 1) / 4;
            source.append(host_ids[parent], host_ids[i]);
            retained.append_child(ids[parent], ids[i]);
        }
    }
    for (std::size_t step = 0; step < 400; ++step) {
        const auto i = std::size_t{1} + random() % (capacity - 1);
        const auto p = random() % capacity;
        auto& cell = source.cells[i];
        bool topology = false;
        if (!cell.active) {
            initialize(i);
            source.append(host_ids[0], host_ids[i]);
            retained.append_child(ids[0], ids[i]);
            topology = true;
        } else switch (random() % 7) {
        case 0:
            cell.style.box.size.width = Length::length(static_cast<Unit>(random() % 100) + .25F);
            cell.style.flex_wrap = random() % 2 ? FlexWrap::Wrap : FlexWrap::NoWrap;
            retained.set_style(ids[i], cell.style);
            session.mark_dirty(host_ids[i]);
            break;
        case 1:
            *cell.width = static_cast<Unit>(random() % 90) + .5F;
            retained.mark_dirty(ids[i]); session.mark_dirty(host_ids[i]);
            break;
        case 2:
            if (source.cells[p].active && source.can_parent(host_ids[p], host_ids[i])) {
                source.append(host_ids[p], host_ids[i]);
                retained.append_child(ids[p], ids[i]); topology = true;
            }
            break;
        case 3:
            source.detach(host_ids[i]); retained.detach(ids[i]); topology = true;
            break;
        case 4: {
            std::reverse(cell.children.begin(), cell.children.end());
            std::vector<NodeId> children;
            for (auto child : cell.children) children.push_back(ids[index(child)]);
            retained.set_children(ids[i], children); topology = true;
            break;
        }
        case 5:
            source.detach(host_ids[i]);
            for (auto child : cell.children) source.cells[index(child)].parent.reset();
            cell.active = false; cell.children.clear();
            retained.remove_node(ids[i]); topology = true;
            break;
        case 6: {
            source.detach(host_ids[i]);
            std::vector<NodeId> pending{host_ids[i]};
            while (!pending.empty()) {
                auto& removed = source.cells[index(pending.back())]; pending.pop_back();
                pending.insert(pending.end(), removed.children.begin(), removed.children.end());
                removed.active = false; removed.children.clear(); removed.parent.reset();
            }
            retained.remove_subtree(ids[i]); topology = true;
            break;
        }
        }
        if (topology) session.invalidate();
        LayoutTree fresh;
        std::array<NodeId, capacity> fresh_ids{};
        for (std::size_t n = 0; n < capacity; ++n) if (source.cells[n].active) {
            fresh_ids[n] = fresh.create_node(source.cells[n].style);
            fresh.set_context(fresh_ids[n], source.cells[n].width);
            require(retained.contains(ids[n]), "Live node disappeared");
        }
        require(fresh.node_count() == retained.node_count(), "Incremental node count differs");
        for (std::size_t n = 0; n < capacity; ++n) if (source.cells[n].active) {
            const auto& model = source.cells[n];
            const auto parent = model.parent ? std::optional<NodeId>{ids[index(*model.parent)]} : std::nullopt;
            require(retained.parent(ids[n]) == parent, "Incremental parent differs from model");
            require(retained.child_count(ids[n]) == model.children.size(), "Incremental children differ from model");
            for (std::size_t j = 0; j < model.children.size(); ++j) {
                const auto child = index(model.children[j]);
                require(retained.child_at(ids[n], j) == ids[child], "Incremental child order differs");
                fresh.append_child(fresh_ids[n], fresh_ids[child]);
            }
        }
        const AvailableSize available{AvailableSpace::definite(200 + static_cast<Unit>(step % 11)), AvailableSpace::max_content()};
        LayoutOptions options;
        options.measure_provider = &provider;
        if (step % 3 == 0) options.rounding = PixelRounding{1.25F};
        for (std::size_t root_index = 0; root_index < capacity; ++root_index) {
            if (!source.cells[root_index].active || source.cells[root_index].parent) continue;
            options.measure_provider = &provider;
            options.publish_nodes = true; options.collect_grid_tracks = true;
            const auto expected = fresh.compute(fresh_ids[root_index], available, options);
            compare(retained.compute(ids[root_index], available, options), expected, ids, fresh_ids);
            options.measure_provider = &borrowed_provider;
            const auto& borrowed = session.compute(host_ids[root_index], available, options);
            compare(borrowed, expected, host_ids, fresh_ids);
            const auto reads = source.reads;
            require(session.compute(host_ids[root_index], available, options).cache_hit && source.reads == reads,
                "Unchanged source unnecessarily reread host nodes");
            if (step % 5 == 0) {
                options.publish_nodes = false; options.collect_grid_tracks = false;
                const auto& sparse = session.compute(host_ids[root_index], available, options);
                require(sparse.succeeded() && sparse.nodes.empty() && sparse.grid_tracks.empty(), "Sparse host publication flattened output");
                for (std::size_t n = 0; n < capacity; ++n) {
                    const auto result = session.layout(host_ids[n]);
                    const auto* expected_node = expected.find(fresh_ids[n]);
                    require(bool(result) == bool(expected_node), "Sparse source node set differs");
                    if (result) require(result->border_box == expected_node->border_box, "Sparse source geometry differs");
                }
            }
        }
    }
    const auto root = host_ids[0];
    require(session.compute(root, {}).succeeded(), "Source snapshot preparation failed");
    const auto previous = session.layout(root);
    require(previous.has_value(), "Source snapshot root missing");
    session.invalidate();
    // Host containers can move their style objects between computations.
    source.cells.reserve(source.cells.capacity() + 100);
    source.fail = true;
    bool failed = false;
    try { (void)session.compute(root, {}); } catch (const std::runtime_error&) { failed = true; }
    require(failed && session.layout(root) == previous, "Source exception changed publication");
    source.fail = false;
    require(session.compute(root, {}).succeeded(), "Source exception prevented recovery");
}
}
int main(int argc, char** argv) {
    const auto seed = argc > 1 ? static_cast<std::uint32_t>(std::stoul(argv[1])) : 1234567U;
    try { stateful(seed); return 0; }
    catch (const std::exception& error) { std::cerr << "Seed " << seed << ": " << error.what() << '\n'; return 1; }
}
