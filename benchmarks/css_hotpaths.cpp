#include <axisly/css_source.hpp>
#include "allocation_probe.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace axisly;
using namespace axisly::css;
using Clock = std::chrono::steady_clock;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
NodeId identity(std::size_t index) {
    return {((static_cast<std::uint64_t>(index + 1) ^ 0xabcdeULL) << 32) | (index + 1)};
}
struct Cell {
    Style style;
    std::optional<NodeId> parent;
    std::vector<NodeId> children;
};
struct Source final : LayoutSource {
    std::vector<Cell> cells;
    std::optional<LayoutNodeView> node(NodeId id) const override {
        const auto index = static_cast<std::uint32_t>(id.value) - std::size_t{1};
        if (index >= cells.size() || identity(index) != id) return std::nullopt;
        const auto& cell = cells[index];
        return LayoutNodeView{cell.style, cell.children, cell.parent};
    }
};
std::size_t slot(NodeId id) { return static_cast<std::uint32_t>(id.value) - std::size_t{1}; }

class Scene {
public:
    Scene(std::string_view name, std::size_t count) : name_(name) {
        Style root;
        root.display = name == "block" ? Display::Block : Display::Flex;
        root.box.size.width = Length::percent(1);
        if (name == "baseline" || name == "hidden") root.align_items = ItemAlignment{ItemPosition::Baseline};
        const auto root_id = add(root, std::nullopt);
        const bool flat = name == "lookup_source" || name == "lookup_owned";
        const std::size_t groups = flat ? 1 : std::min(std::size_t{16}, count);
        for (std::size_t group = 0; group < groups; ++group) {
            auto parent = root_id;
            if (!flat) {
                Style branch;
                branch.display = name == "block" ? Display::Block : Display::Flex;
                branch.box.size.width = Length::length(128.25F);
                branch.flex_shrink = 0;
                branch.flex_wrap = FlexWrap::Wrap;
                if (name == "hidden") branch.display = Display::Block;
                if ((name == "tracks_mixed" || name == "tracks_source") && group == 0) {
                    branch.display = Display::Grid;
                    branch.grid.template_columns.assign(2, TrackSizing::fraction(1));
                }
                parent = add(branch, root_id);
                if (name == "hidden") {
                    Style visible;
                    visible.box.size = {Length::length(20), Length::length(12)};
                    add(visible, parent);
                    Style hidden;
                    hidden.display = Display::None;
                    parent = add(hidden, parent);
                }
            }
            for (std::size_t i = group; i < count; i += groups) {
                Style leaf;
                leaf.display = Display::Block;
                leaf.box.size = {Length::length(20.25F), Length::length(12.5F)};
                leaf.flex_shrink = 0;
                leaves_.push_back(add(leaf, parent));
            }
        }
        if (name == "lookup_source" || name == "tracks_source") session_ = std::make_unique<LayoutSession>(source_);
        else {
            tree_ = std::make_unique<LayoutTree>();
            for (const auto& cell : source_.cells) ids_.push_back(tree_->create_node(cell.style));
            for (std::size_t i = 0; i < source_.cells.size(); ++i) {
                std::vector<NodeId> children;
                for (const auto id : source_.cells[i].children) children.push_back(ids_[slot(id)]);
                if (!children.empty()) tree_->set_children(ids_[i], children);
            }
        }
        options_.collect_grid_tracks = false;
        if (name == "rounded" || name == "rounded_sparse") options_.rounding = PixelRounding{1.25F, 0.2F, -0.3F};
        options_.publish_nodes = name != "rounded_sparse";
    }
    const LayoutResult& step(std::string_view mode, std::size_t frame, bool invalidate = false) {
        const bool tracks = name_.starts_with("tracks_");
        if (tracks) options_.collect_grid_tracks = frame % 2 == 0;
        if (mode == "edit" && !tracks) {
            const auto id = leaves_[frame % leaves_.size()];
            auto& style = source_.cells[slot(id)].style;
            style.box.size.height = style.box.size.height == Length::length(12.5F)
                ? Length::length(13.25F) : Length::length(12.5F);
            if (session_) session_->mark_dirty(id);
            else tree_->set_style(ids_[slot(id)], style);
        }
        if (invalidate) {
            if (session_) session_->invalidate();
            else tree_->mark_subtree_dirty(ids_[0]);
        }
        const AvailableSize available{AvailableSpace::definite(2200.5F
            + (mode == "resize" ? static_cast<Unit>(frame % 17) : 0)), AvailableSpace::max_content()};
        const auto& result = session_ ? session_->compute(identity(0), available, options_)
            : tree_->compute(ids_[0], available, options_);
        require(result.succeeded(), "Hot-path layout failed");
        if (tracks) require(result.grid_tracks.size() == (options_.collect_grid_tracks && name_ != "tracks_flex" ? 1U : 0U),
            "Diagnostic toggle produced incomplete reports");
        return result;
    }
    double consume(const LayoutResult& result, std::size_t queries) const {
        double sum = result.size.width + result.size.height;
        if (!queries) queries = source_.cells.size();
        for (std::size_t i = 0; i < queries; ++i) {
            const auto index = (i * 7919 + 17) % source_.cells.size();
            const auto value = session_ ? session_->layout(identity(index)) : tree_->layout(ids_[index]);
            require(value.has_value(), "Hot-path lookup failed");
            sum += value->border_box.x + value->border_box.y + value->border_box.width + value->border_box.height;
        }
        return sum;
    }
    std::vector<NodeLayout> geometry() const {
        std::vector<NodeLayout> result;
        for (std::size_t i = 0; i < source_.cells.size(); ++i) {
            auto value = session_ ? session_->layout(identity(i)) : tree_->layout(ids_[i]);
            require(value.has_value(), "Missing validation node");
            value->node = identity(i);
            value->parent = source_.cells[i].parent;
            result.push_back(*value);
        }
        return result;
    }
private:
    NodeId add(const Style& style, std::optional<NodeId> parent) {
        const auto id = identity(source_.cells.size());
        source_.cells.push_back({style, parent, {}});
        if (parent) source_.cells[slot(*parent)].children.push_back(id);
        return id;
    }
    std::string_view name_;
    Source source_;
    std::vector<NodeId> ids_, leaves_;
    std::unique_ptr<LayoutTree> tree_;
    std::unique_ptr<LayoutSession> session_;
    LayoutOptions options_;
};

class MutationScene {
public:
    MutationScene(std::string_view name, std::size_t count) : deep_(name == "deep_reorder") {
        root_ = tree_.create_node();
        auto parent = root_;
        if (deep_) for (std::size_t i = 0; i < 32; ++i) {
            const auto child = tree_.create_node();
            tree_.append_child(parent, child);
            parent = child;
        }
        first_ = tree_.create_node(); second_ = tree_.create_node();
        tree_.set_children(parent, std::array{first_, second_});
        children_.reserve(count);
        Style leaf;
        leaf.box.size = {Length::length(2), Length::length(3)};
        for (std::size_t i = 0; i < count; ++i) children_.push_back(tree_.create_node(leaf));
        tree_.set_children(first_, children_);
        current_ = first_;
    }
    double step(bool same) {
        if (!same) {
            if (deep_) std::reverse(children_.begin(), children_.end());
            else current_ = current_ == first_ ? second_ : first_;
        }
        tree_.set_children(current_, children_);
        return static_cast<double>(tree_.child_count(current_))
            + static_cast<double>(tree_.child_at(current_, 0).value)
            + static_cast<double>(tree_.parent(children_.back())->value);
    }
    void validate() {
        require(std::equal(children_.begin(), children_.end(), tree_.children(current_).begin(),
            tree_.children(current_).end()), "Batch mutation changed child order");
        for (const auto child : children_) require(tree_.parent(child) == current_, "Batch mutation lost parent");
        const auto incremental = tree_.compute(root_, {});
        tree_.mark_subtree_dirty(root_);
        const auto& fresh = tree_.compute(root_, {});
        require(incremental.succeeded() && incremental.nodes == fresh.nodes,
            "Batch mutation reused stale geometry");
    }
private:
    LayoutTree tree_;
    NodeId root_, first_, second_, current_;
    std::vector<NodeId> children_;
    bool deep_;
};

void run_mutation(std::string_view name, std::string_view mode, std::size_t count,
                  std::size_t iterations, std::size_t samples, std::size_t queries) {
    MutationScene validation(name, std::min(count, std::size_t{64}));
    for (std::size_t i = 0; i < 5; ++i) { (void)validation.step(mode == "same"); validation.validate(); }
    for (std::size_t sample = 0; sample < samples; ++sample) {
        MutationScene scene(name, count);
        double micros = 0, checksum = 0;
        std::size_t allocations = 0, bytes = 0;
        for (std::size_t i = 0; i < iterations; ++i) {
#ifdef AXISLY_COUNT_ALLOCATIONS
            axisly::bench::begin_allocations();
#endif
            const auto start = Clock::now();
            checksum += scene.step(mode == "same");
            micros += std::chrono::duration<double, std::micro>(Clock::now() - start).count();
#ifdef AXISLY_COUNT_ALLOCATIONS
            const auto allocation = axisly::bench::end_allocations();
            allocations += allocation.allocations; bytes += allocation.requested_bytes;
#endif
        }
        const auto divisor = static_cast<double>(iterations);
        std::cout << name << ',' << mode << ',' << count << ',' << queries << ',' << sample << ','
            << micros / divisor << ',' << allocations / divisor << ',' << bytes / divisor
            << ",0,0," << checksum << '\n';
    }
}

void validate(std::string_view name, std::string_view mode, std::size_t count) {
    Scene retained(name, count), fresh(name, count);
    for (std::size_t frame = 0; frame < 5; ++frame) {
        retained.step(mode, frame);
        fresh.step(mode, frame, true);
        require(retained.geometry() == fresh.geometry(), "Incremental geometry differs from invalidated tree");
    }
}
void run(std::string_view name, std::string_view mode, std::size_t count,
         std::size_t iterations, std::size_t samples, std::size_t queries) {
    for (std::size_t sample = 0; sample < samples; ++sample) {
        auto scene = std::make_unique<Scene>(name, count);
        if (mode != "cold") for (std::size_t frame = 0; frame < 4; ++frame) {
            const auto& result = scene->step(mode, frame);
            (void)scene->consume(result, queries);
        }
        double micros = 0, checksum = 0;
        std::size_t allocations = 0, bytes = 0, layouts = 0, reused = 0;
        for (std::size_t frame = 0; frame < iterations; ++frame) {
            if (mode == "cold" && frame) scene = std::make_unique<Scene>(name, count);
#ifdef AXISLY_COUNT_ALLOCATIONS
            axisly::bench::begin_allocations();
#endif
            const auto start = Clock::now();
            const auto& result = scene->step(mode, frame + 4);
            checksum += scene->consume(result, queries);
            micros += std::chrono::duration<double, std::micro>(Clock::now() - start).count();
#ifdef AXISLY_COUNT_ALLOCATIONS
            const auto allocation = axisly::bench::end_allocations();
            allocations += allocation.allocations;
            bytes += allocation.requested_bytes;
#endif
            layouts += result.layout_count;
            reused += result.published_nodes_reused;
        }
        const auto divisor = static_cast<double>(iterations);
        std::cout << name << ',' << mode << ',' << count << ',' << queries << ',' << sample << ','
            << micros / divisor << ',' << allocations / divisor << ',' << bytes / divisor << ','
            << layouts / divisor << ',' << reused / divisor << ',' << checksum << '\n';
    }
}
}

int main(int argc, char** argv) {
    try {
        std::string name = "lookup_source", mode = "edit";
        std::size_t count = 1000, iterations = 12, samples = 9, queries = 3;
        bool smoke = false;
        for (int i = 1; i < argc; ++i) {
            const std::string_view argument = argv[i];
            if (argument == "--smoke") { smoke = true; continue; }
            require(i + 1 < argc, "Missing option value");
            const std::string value = argv[++i];
            if (argument == "--case") name = value;
            else if (argument == "--mode") mode = value;
            else if (argument == "--count") count = std::stoull(value);
            else if (argument == "--iterations") iterations = std::stoull(value);
            else if (argument == "--samples") samples = std::stoull(value);
            else if (argument == "--queries") queries = std::stoull(value);
            else throw std::invalid_argument("Unknown option");
        }
        require(count > 0 && count <= 10000 && iterations > 0 && samples > 0, "Invalid benchmark size");
        require(mode == "cold" || mode == "edit" || mode == "resize" || mode == "same", "Unknown mode");
        const std::vector<std::string_view> cases{"lookup_source", "lookup_owned", "block", "rounded", "rounded_sparse", "baseline",
            "hidden", "tracks_mixed", "tracks_flex", "tracks_source", "batch_move", "deep_reorder"};
        require(std::find(cases.begin(), cases.end(), name) != cases.end(), "Unknown case");
        std::cout << std::setprecision(12) << "case,mode,count,queries,sample,us,allocations,bytes,layouts,reused,checksum\n";
        if (smoke) {
            for (const auto item : cases) for (const auto scenario : {"cold", "edit", "resize", "same"}) {
                if (item == "batch_move" || item == "deep_reorder") {
                    run_mutation(item, scenario, 48, 2, 1, 3);
                    continue;
                }
                validate(item, scenario, 48);
                run(item, scenario, 48, 2, 1, 3);
            }
        } else if (name == "batch_move" || name == "deep_reorder") {
            run_mutation(name, mode, count, iterations, samples, queries);
        } else {
            validate(name, mode, std::min(count, std::size_t{100}));
            run(name, mode, count, iterations, samples, queries);
        }
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
