#include <axisly/flex.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_set>

namespace axisly {
namespace {

constexpr double infinity = std::numeric_limits<double>::infinity();
bool finite(Unit x) noexcept { return std::isfinite(x); }
bool nonnegative(Unit x) noexcept { return finite(x) && x >= 0; }
bool insets_ok(const Insets& x) noexcept {
    return nonnegative(x.left) && nonnegative(x.top) && nonnegative(x.right) && nonnegative(x.bottom);
}
bool unit_ok(double x) noexcept {
    return std::isfinite(x) && x >= -std::numeric_limits<Unit>::max()
        && x <= std::numeric_limits<Unit>::max();
}
bool size_ok(double x) noexcept { return unit_ok(x) && x >= 0; }
template<class E> bool enum_ok(E x, E maximum) noexcept {
    return static_cast<unsigned>(x) <= static_cast<unsigned>(maximum);
}
bool length_ok(FlexLength x, bool negative) noexcept {
    if (!enum_ok(x.kind, FlexLength::Kind::FitContent) || !finite(x.value)) return false;
    if (x.kind == FlexLength::Kind::Fixed || x.kind == FlexLength::Kind::Percent)
        return negative || x.value >= 0;
    return true;
}
bool edges_ok(const FlexEdges& x, bool negative) noexcept {
    return length_ok(x.left, negative) && length_ok(x.top, negative)
        && length_ok(x.right, negative) && length_ok(x.bottom, negative);
}
bool alignment_ok(FlexAlignment x) noexcept {
    return enum_ok(x.value, FlexAlign::SpaceEvenly);
}
bool measurement_ok(const FlexMeasureResult& r) noexcept {
    const auto size = [](Size x) { return nonnegative(x.width) && nonnegative(x.height); };
    return size(r.min_content) && size(r.max_content) && size(r.preferred)
        && r.min_content.width <= r.max_content.width
        && r.min_content.height <= r.max_content.height
        && (!r.first_baseline || (nonnegative(*r.first_baseline) && *r.first_baseline <= r.preferred.height))
        && (!r.last_baseline || (nonnegative(*r.last_baseline) && *r.last_baseline <= r.preferred.height));
}
FlexResult failure(FlexErrorCode code, const char* message,
                   std::optional<ItemId> id = {}, std::size_t measurements = 0) {
    FlexResult result;
    result.error = {code, message, id};
    result.measurement_count = measurements;
    return result;
}

struct Axes {
    bool main_horizontal{};
    int main_sign{1}, cross_sign{1};
    double physical_x{}, physical_y{}, physical_width{}, physical_height{};
    double main_extent{}, cross_extent{};
    double main_gap{}, cross_gap{};

    Rect rect(double main, double cross, double main_size, double cross_size) const noexcept {
        const double m = main_horizontal
            ? physical_x + (main_sign > 0 ? main : main_extent - main - main_size)
            : physical_y + (main_sign > 0 ? main : main_extent - main - main_size);
        const double c = main_horizontal
            ? physical_y + (cross_sign > 0 ? cross : cross_extent - cross - cross_size)
            : physical_x + (cross_sign > 0 ? cross : cross_extent - cross - cross_size);
        return main_horizontal
            ? Rect{static_cast<Unit>(m), static_cast<Unit>(c),
                   static_cast<Unit>(main_size), static_cast<Unit>(cross_size)}
            : Rect{static_cast<Unit>(c), static_cast<Unit>(m),
                   static_cast<Unit>(cross_size), static_cast<Unit>(main_size)};
    }
};
Axes make_axes(const FlexSpec& spec, Size available) noexcept {
    const double x = static_cast<double>(spec.border.left) + spec.padding.left;
    const double y = static_cast<double>(spec.border.top) + spec.padding.top;
    const double width = std::max(0.0, static_cast<double>(available.width)
        - x - spec.border.right - spec.padding.right);
    const double height = std::max(0.0, static_cast<double>(available.height)
        - y - spec.border.bottom - spec.padding.bottom);
    const bool row = spec.direction == FlexDirection::Row || spec.direction == FlexDirection::RowReverse;
    const bool horizontal_writing = spec.writing_mode == WritingMode::HorizontalTb;
    const bool inline_horizontal = horizontal_writing;
    const int inline_sign = horizontal_writing ? (spec.text_direction == TextDirection::Ltr ? 1 : -1)
        : spec.writing_mode == WritingMode::SidewaysLr
            ? (spec.text_direction == TextDirection::Ltr ? -1 : 1)
            : (spec.text_direction == TextDirection::Ltr ? 1 : -1);
    const int block_sign = horizontal_writing || spec.writing_mode == WritingMode::VerticalLr
        || spec.writing_mode == WritingMode::SidewaysLr ? 1 : -1;
    const bool main_horizontal = row ? inline_horizontal : !inline_horizontal;
    int main_sign = row ? inline_sign : block_sign;
    int cross_sign = row ? block_sign : inline_sign;
    if (spec.direction == FlexDirection::RowReverse || spec.direction == FlexDirection::ColumnReverse)
        main_sign = -main_sign;
    if (spec.wrap == FlexWrap::WrapReverse) cross_sign = -cross_sign;
    return {main_horizontal, main_sign, cross_sign, x, y, width, height,
        main_horizontal ? width : height, main_horizontal ? height : width,
        row ? spec.column_gap : spec.row_gap, row ? spec.row_gap : spec.column_gap};
}
double axis_inset(const Insets& x, bool horizontal) noexcept {
    return horizontal ? static_cast<double>(x.left) + x.right
        : static_cast<double>(x.top) + x.bottom;
}
double axis_start(const Insets& x, bool horizontal, int sign) noexcept {
    return horizontal ? (sign > 0 ? x.left : x.right) : (sign > 0 ? x.top : x.bottom);
}
FlexLength dimension(const FlexItem& x, bool horizontal, int which) noexcept {
    if (which == 0) return horizontal ? x.width : x.height;
    if (which == 1) return horizontal ? x.minimum_width : x.minimum_height;
    return horizontal ? x.maximum_width : x.maximum_height;
}
double measure_axis(Size x, bool horizontal) noexcept { return horizontal ? x.width : x.height; }
FlexLength edge(const FlexEdges& x, bool horizontal, int sign, bool start) noexcept {
    return horizontal ? (sign > 0 ? (start ? x.left : x.right) : (start ? x.right : x.left))
        : (sign > 0 ? (start ? x.top : x.bottom) : (start ? x.bottom : x.top));
}
std::optional<double> resolve(FlexLength x, double basis, double min_content, double max_content,
                              double padding_border = 0, FlexBoxSizing box_sizing = FlexBoxSizing::ContentBox) noexcept {
    double value;
    switch (x.kind) {
    case FlexLength::Kind::Auto: return std::nullopt;
    case FlexLength::Kind::Fixed: value = x.value; break;
    case FlexLength::Kind::Percent: value = static_cast<double>(x.value) * basis; break;
    case FlexLength::Kind::Content:
    case FlexLength::Kind::MaxContent: value = max_content; break;
    case FlexLength::Kind::MinContent: value = min_content; break;
    case FlexLength::Kind::FitContent: value = std::clamp(basis, min_content, max_content); break;
    default: return std::nullopt;
    }
    if (box_sizing == FlexBoxSizing::BorderBox
        && (x.kind == FlexLength::Kind::Fixed || x.kind == FlexLength::Kind::Percent))
        value = std::max(0.0, value - padding_border);
    return value;
}
double margin(FlexLength x, double inline_basis) noexcept {
    if (x.kind == FlexLength::Kind::Fixed) return x.value;
    if (x.kind == FlexLength::Kind::Percent) return x.value * inline_basis;
    return 0;
}
bool auto_margin(FlexLength x) noexcept { return x.kind == FlexLength::Kind::Auto; }
FlexMeasureResult static_measurement(const FlexItem& item) noexcept {
    return {item.min_content,
        {std::max({item.max_content.width, item.min_content.width, item.preferred_size.width}),
         std::max({item.max_content.height, item.min_content.height, item.preferred_size.height})},
        item.preferred_size, item.baseline, item.baseline};
}

struct State {
    std::size_t source{};
    FlexMeasureResult measured{};
    double main_bp{}, cross_bp{}, main_bp_start{}, cross_bp_start{};
    double margin_main_start{}, margin_main_end{}, margin_cross_start{}, margin_cross_end{};
    bool main_auto_start{}, main_auto_end{}, cross_auto_start{}, cross_auto_end{};
    double base{}, min_main{}, max_main{infinity}, hypo{}, target{};
    double min_cross{}, max_cross{infinity}, cross{};
    bool cross_auto{};
    double baseline{};
    std::size_t line{};
    double main_pos{}, cross_pos{};
    bool frozen{};
};
struct Line {
    std::vector<std::size_t> states;
    double cross{}, cross_pos{};
    double baseline_ascent{}, baseline_descent{};
};

bool is_baseline(FlexAlign value) noexcept {
    return value == FlexAlign::FirstBaseline || value == FlexAlign::LastBaseline;
}
FlexAlign logical_alignment(FlexAlign value, bool reverse) noexcept {
    if (value == FlexAlign::Start) return reverse ? FlexAlign::FlexEnd : FlexAlign::FlexStart;
    if (value == FlexAlign::End) return reverse ? FlexAlign::FlexStart : FlexAlign::FlexEnd;
    return value;
}
FlexAlign resolved_align(const FlexSpec& spec, const FlexItem& item) noexcept {
    FlexAlign value = item.align_self.value == FlexAlign::Auto
        ? spec.align_items.value : item.align_self.value;
    if (value == FlexAlign::Normal) value = FlexAlign::Stretch;
    return value;
}
void alignment_offset(FlexAlign align, double free_space, std::size_t count,
                      double& offset, double& between) noexcept {
    offset = between = 0;
    switch (align) {
    case FlexAlign::End:
    case FlexAlign::FlexEnd: offset = free_space; break;
    case FlexAlign::Center: offset = free_space / 2; break;
    case FlexAlign::SpaceBetween:
        if (count > 1 && free_space > 0) between = free_space / static_cast<double>(count - 1);
        break;
    case FlexAlign::SpaceAround:
        if (count && free_space > 0) {
            between = free_space / static_cast<double>(count);
            offset = between / 2;
        } else offset = free_space / 2;
        break;
    case FlexAlign::SpaceEvenly:
        if (count && free_space > 0) offset = between = free_space / static_cast<double>(count + 1);
        else offset = free_space / 2;
        break;
    default: break;
    }
}
} // namespace

FlexResult compute_flex(const FlexSpec& spec, std::span<const FlexItem> items, Size available) {
    return compute_flex(spec, items, available, nullptr, nullptr);
}

FlexResult compute_flex(const FlexSpec& spec, std::span<const FlexItem> items,
                        Size available, FlexMeasureCallback measure, void* user_data) {
    if (!nonnegative(available.width) || !nonnegative(available.height))
        return failure(FlexErrorCode::InvalidContainerSize, "Container size must be finite and non-negative");
    if (!enum_ok(spec.direction, FlexDirection::ColumnReverse)
        || !enum_ok(spec.wrap, FlexWrap::WrapReverse)
        || !enum_ok(spec.writing_mode, WritingMode::SidewaysLr)
        || !enum_ok(spec.text_direction, TextDirection::Rtl)
        || !alignment_ok(spec.justify_content) || !alignment_ok(spec.align_items)
        || !alignment_ok(spec.align_content) || !nonnegative(spec.column_gap)
        || !nonnegative(spec.row_gap) || !insets_ok(spec.padding) || !insets_ok(spec.border))
        return failure(FlexErrorCode::InvalidContainerStyle, "Invalid flex container style");

    std::unordered_set<ItemId> ids;
    ids.reserve(items.size());
    for (const auto& item : items) {
        if (!ids.insert(item.id).second)
            return failure(FlexErrorCode::DuplicateItemId, "Item IDs must be unique", item.id);
        if (!nonnegative(item.grow) || !nonnegative(item.shrink)
            || !length_ok(item.basis, false) || !length_ok(item.width, false)
            || !length_ok(item.height, false) || !length_ok(item.minimum_width, false)
            || !length_ok(item.minimum_height, false) || !length_ok(item.maximum_width, false)
            || !length_ok(item.maximum_height, false) || !edges_ok(item.margin, true)
            || !edges_ok(item.inset, true) || !insets_ok(item.padding)
            || !insets_ok(item.border) || !alignment_ok(item.align_self)
            || !enum_ok(item.position, FlexPosition::Absolute)
            || !enum_ok(item.visibility, FlexVisibility::Collapse)
            || !enum_ok(item.overflow, FlexOverflow::Clip)
            || !enum_ok(item.box_sizing, FlexBoxSizing::BorderBox)
            || (item.aspect_ratio && (!finite(*item.aspect_ratio) || *item.aspect_ratio <= 0))
            || !nonnegative(item.min_content.width) || !nonnegative(item.min_content.height)
            || !nonnegative(item.max_content.width) || !nonnegative(item.max_content.height)
            || !nonnegative(item.preferred_size.width) || !nonnegative(item.preferred_size.height)
            || (item.baseline && (!nonnegative(*item.baseline) || *item.baseline > item.preferred_size.height)))
            return failure(FlexErrorCode::InvalidItemStyle, "Invalid flex item style", item.id);
        for (const FlexLength length : {item.margin.left, item.margin.top, item.margin.right, item.margin.bottom,
                                        item.inset.left, item.inset.top, item.inset.right, item.inset.bottom}) {
            if (length.kind != FlexLength::Kind::Auto && length.kind != FlexLength::Kind::Fixed
                && length.kind != FlexLength::Kind::Percent)
                return failure(FlexErrorCode::InvalidItemStyle, "Margins and offsets require auto, length, or percent", item.id);
        }
    }

    const Axes axes = make_axes(spec, available);
    std::vector<double> collapsed_struts(items.size(), 0);
    std::size_t first_round_measurements = 0;
    if (std::any_of(items.begin(), items.end(), [](const FlexItem& item) {
            return item.position == FlexPosition::Normal
                && item.visibility == FlexVisibility::Collapse;
        })) {
        std::vector<FlexItem> uncollapsed(items.begin(), items.end());
        for (auto& item : uncollapsed) item.visibility = FlexVisibility::Visible;
        const auto first = compute_flex(spec, uncollapsed, available, measure, user_data);
        if (!first.succeeded()) return first;
        first_round_measurements = first.measurement_count;
        for (std::size_t index = 0; index < items.size(); ++index) {
            if (items[index].visibility != FlexVisibility::Collapse
                || items[index].position != FlexPosition::Normal) continue;
            const auto line = first.items[index].line_index;
            if (line < first.lines.size())
                collapsed_struts[index] = axes.main_horizontal
                    ? first.lines[line].rect.height : first.lines[line].rect.width;
        }
    }
    const double inline_extent = spec.writing_mode == WritingMode::HorizontalTb
        ? axes.physical_width : axes.physical_height;
    std::vector<std::size_t> order(items.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(),
        [&](std::size_t a, std::size_t b) {
            const int a_order = items[a].position == FlexPosition::Absolute ? 0 : items[a].order;
            const int b_order = items[b].position == FlexPosition::Absolute ? 0 : items[b].order;
            return a_order < b_order;
        });
    FlexResult result;
    result.size = available;
    result.measurement_count = first_round_measurements;
    result.paint_order.reserve(items.size());
    result.items.resize(items.size());
    std::vector<State> states(items.size());
    for (const auto index : order) {
        const auto& item = items[index];
        result.paint_order.push_back(item.id);
        State& state = states[index];
        state.source = index;
        state.measured = static_measurement(item);
        if (measure) {
            FlexMeasureInput input;
            input.available_content_width = static_cast<Unit>(axes.physical_width);
            input.available_content_height = static_cast<Unit>(axes.physical_height);
            input.writing_mode = spec.writing_mode;
            // A stretched column item has a definite cross width before its
            // content-based main height is measured (notably wrapped text).
            if (!axes.main_horizontal) {
                const auto width = item.width;
                const double horizontal_bp = axis_inset(item.padding, true)
                    + axis_inset(item.border, true);
                const double horizontal_margin = margin(item.margin.left, inline_extent)
                    + margin(item.margin.right, inline_extent);
                std::optional<double> known_width;
                if (width.kind == FlexLength::Kind::Fixed)
                    known_width = item.box_sizing == FlexBoxSizing::BorderBox
                        ? std::max(0.0, static_cast<double>(width.value) - horizontal_bp)
                        : static_cast<double>(width.value);
                else if (width.kind == FlexLength::Kind::Percent)
                    known_width = item.box_sizing == FlexBoxSizing::BorderBox
                        ? std::max(0.0, static_cast<double>(width.value) * axes.physical_width - horizontal_bp)
                        : static_cast<double>(width.value) * axes.physical_width;
                else if (width.kind == FlexLength::Kind::Auto
                    && resolved_align(spec, item) == FlexAlign::Stretch
                    && !auto_margin(item.margin.left) && !auto_margin(item.margin.right))
                    known_width = std::max(0.0, axes.physical_width
                        - horizontal_bp - horizontal_margin);
                if (known_width && size_ok(*known_width))
                    input.known_content_width = static_cast<Unit>(*known_width);
            }
            state.measured = measure(item.id, input, user_data);
            ++result.measurement_count;
            if (!measurement_ok(state.measured))
                return failure(FlexErrorCode::InvalidMeasurement, "Invalid intrinsic measurement", item.id, result.measurement_count);
        }
        state.main_bp = axis_inset(item.padding, axes.main_horizontal)
            + axis_inset(item.border, axes.main_horizontal);
        state.cross_bp = axis_inset(item.padding, !axes.main_horizontal)
            + axis_inset(item.border, !axes.main_horizontal);
        state.main_bp_start = axis_start(item.padding, axes.main_horizontal, axes.main_sign)
            + axis_start(item.border, axes.main_horizontal, axes.main_sign);
        state.cross_bp_start = axis_start(item.padding, !axes.main_horizontal, axes.cross_sign)
            + axis_start(item.border, !axes.main_horizontal, axes.cross_sign);
        const auto mms = edge(item.margin, axes.main_horizontal, axes.main_sign, true);
        const auto mme = edge(item.margin, axes.main_horizontal, axes.main_sign, false);
        const auto mcs = edge(item.margin, !axes.main_horizontal, axes.cross_sign, true);
        const auto mce = edge(item.margin, !axes.main_horizontal, axes.cross_sign, false);
        state.main_auto_start = auto_margin(mms);
        state.main_auto_end = auto_margin(mme);
        state.cross_auto_start = auto_margin(mcs);
        state.cross_auto_end = auto_margin(mce);
        state.margin_main_start = margin(mms, inline_extent);
        state.margin_main_end = margin(mme, inline_extent);
        state.margin_cross_start = margin(mcs, inline_extent);
        state.margin_cross_end = margin(mce, inline_extent);
        const double main_min_content = measure_axis(state.measured.min_content, axes.main_horizontal);
        const double main_max_content = measure_axis(state.measured.max_content, axes.main_horizontal);
        const double cross_min_content = measure_axis(state.measured.min_content, !axes.main_horizontal);
        const double cross_max_content = measure_axis(state.measured.max_content, !axes.main_horizontal);
        state.min_cross = resolve(dimension(item, !axes.main_horizontal, 1), axes.cross_extent,
            cross_min_content, cross_max_content, state.cross_bp, item.box_sizing).value_or(0);
        state.max_cross = resolve(dimension(item, !axes.main_horizontal, 2), axes.cross_extent,
            cross_min_content, cross_max_content, state.cross_bp, item.box_sizing).value_or(infinity);
        state.max_cross = std::max(state.min_cross, state.max_cross);
        const auto specified_main = resolve(dimension(item, axes.main_horizontal, 0), axes.main_extent,
            main_min_content, main_max_content, state.main_bp, item.box_sizing);
        const auto specified_cross = resolve(dimension(item, !axes.main_horizontal, 0), axes.cross_extent,
            cross_min_content, cross_max_content, state.cross_bp, item.box_sizing);
        auto base = resolve(item.basis, axes.main_extent, main_min_content, main_max_content);
        if (!base && specified_main) base = specified_main;
        if (!base && item.aspect_ratio && specified_cross) {
            base = axes.main_horizontal ? *specified_cross * *item.aspect_ratio
                : *specified_cross / *item.aspect_ratio;
        }
        state.base = base.value_or(main_max_content);
        auto min_main = resolve(dimension(item, axes.main_horizontal, 1), axes.main_extent,
            main_min_content, main_max_content, state.main_bp, item.box_sizing);
        if (!min_main) {
            const bool non_scrollable = item.overflow == FlexOverflow::Visible
                || item.overflow == FlexOverflow::Clip;
            state.min_main = non_scrollable ? main_min_content : 0;
            if (non_scrollable && item.aspect_ratio && specified_cross) {
                const double cross = std::clamp(*specified_cross,
                    state.min_cross, state.max_cross);
                const double transferred = axes.main_horizontal
                    ? cross * *item.aspect_ratio : cross / *item.aspect_ratio;
                state.min_main = item.replaced
                    ? std::min(state.min_main, transferred)
                    : std::max(state.min_main, transferred);
            }
            if (non_scrollable && specified_main)
                state.min_main = std::min(state.min_main, *specified_main);
        } else state.min_main = *min_main;
        state.max_main = resolve(dimension(item, axes.main_horizontal, 2), axes.main_extent,
            main_min_content, main_max_content, state.main_bp, item.box_sizing).value_or(infinity);
        state.max_main = std::max(state.min_main, state.max_main);
        state.hypo = std::clamp(state.base, state.min_main, state.max_main);
        state.target = state.hypo;
        state.cross_auto = !specified_cross.has_value();
        state.cross = std::clamp(specified_cross.value_or(
            measure_axis(state.measured.preferred, !axes.main_horizontal)),
            state.min_cross, state.max_cross);
        if (!size_ok(state.base) || !size_ok(state.hypo) || !size_ok(state.cross)
            || !unit_ok(state.margin_main_start) || !unit_ok(state.margin_main_end)
            || !unit_ok(state.margin_cross_start) || !unit_ok(state.margin_cross_end))
            return failure(FlexErrorCode::NumericOverflow, "Flex item geometry overflow", item.id, result.measurement_count);
    }

    std::vector<Line> lines;
    std::vector<std::size_t> flow;
    flow.reserve(items.size());
    for (auto index : order) if (items[index].position == FlexPosition::Normal) flow.push_back(index);
    if (!flow.empty()) lines.emplace_back();
    double line_used = 0;
    for (auto index : flow) {
        State& state = states[index];
        const bool collapsed = items[index].visibility == FlexVisibility::Collapse;
        const double outer = collapsed ? 0 : state.hypo + state.main_bp
            + state.margin_main_start + state.margin_main_end;
        const double next = lines.back().states.empty() ? outer : line_used + axes.main_gap + outer;
        if (spec.wrap != FlexWrap::NoWrap && !lines.back().states.empty()
            && next > axes.main_extent) {
            lines.emplace_back();
            line_used = 0;
        }
        Line& line = lines.back();
        if (!line.states.empty()) line_used += axes.main_gap;
        line_used += outer;
        state.line = lines.size() - 1;
        line.states.push_back(index);
    }
    if (lines.empty() && !flow.empty()) lines.emplace_back();

    for (auto& line : lines) {
        std::vector<std::size_t> visible;
        for (auto index : line.states) {
            if (items[index].visibility == FlexVisibility::Visible) visible.push_back(index);
            else states[index].target = 0;
        }
        const double gaps = visible.size() > 1
            ? axes.main_gap * static_cast<double>(visible.size() - 1) : 0;
        double hypothetical_outer = gaps;
        for (auto index : visible) {
            const State& state = states[index];
            hypothetical_outer += state.hypo + state.main_bp
                + state.margin_main_start + state.margin_main_end;
        }
        const bool grow = hypothetical_outer < axes.main_extent;
        const double initial_free = axes.main_extent - hypothetical_outer;
        for (auto index : visible) {
            State& state = states[index];
            const double factor = grow ? items[index].grow : items[index].shrink;
            state.frozen = factor == 0 || (grow ? state.base > state.hypo : state.base < state.hypo);
            state.target = state.frozen ? state.hypo : state.base;
        }
        for (std::size_t iteration = 0; iteration <= visible.size(); ++iteration) {
            bool any = false;
            double used = gaps, factor_sum = 0;
            for (auto index : visible) {
                const State& state = states[index];
                used += (state.frozen ? state.target : state.base)
                    + state.main_bp + state.margin_main_start + state.margin_main_end;
                if (!state.frozen) {
                    any = true;
                    factor_sum += grow ? items[index].grow : items[index].shrink * state.base;
                }
            }
            if (!any) break;
            double remaining = axes.main_extent - used;
            double raw_factor_sum = 0;
            for (auto index : visible) if (!states[index].frozen)
                raw_factor_sum += grow ? items[index].grow : items[index].shrink;
            if (raw_factor_sum < 1)
                remaining = grow ? std::min(remaining, initial_free * raw_factor_sum)
                    : std::max(remaining, initial_free * raw_factor_sum);
            double total_violation = 0;
            std::vector<double> violations(visible.size(), 0);
            for (std::size_t i = 0; i < visible.size(); ++i) {
                State& state = states[visible[i]];
                if (state.frozen) continue;
                const double factor = grow ? items[visible[i]].grow
                    : items[visible[i]].shrink * state.base;
                const double provisional = state.base
                    + (factor_sum > 0 ? remaining * factor / factor_sum : 0);
                state.target = std::clamp(provisional, state.min_main, state.max_main);
                violations[i] = state.target - provisional;
                total_violation += violations[i];
            }
            bool froze = false;
            for (std::size_t i = 0; i < visible.size(); ++i) {
                State& state = states[visible[i]];
                if (state.frozen) continue;
                if (total_violation == 0
                    || (total_violation > 0 && violations[i] > 0)
                    || (total_violation < 0 && violations[i] < 0)) {
                    state.frozen = true;
                    froze = true;
                }
            }
            if (!froze) break;
        }
        for (auto index : line.states) {
            State& state = states[index];
            const auto& item = items[index];
            if (item.visibility == FlexVisibility::Collapse) continue;
            if (item.visibility == FlexVisibility::Visible && measure) {
                FlexMeasureInput input;
                if (axes.main_horizontal) input.known_content_width = static_cast<Unit>(state.target);
                else input.known_content_height = static_cast<Unit>(state.target);
                if (axes.main_horizontal) input.available_content_height = static_cast<Unit>(axes.cross_extent);
                else input.available_content_width = static_cast<Unit>(axes.cross_extent);
                input.writing_mode = spec.writing_mode;
                if (!size_ok(state.target))
                    return failure(FlexErrorCode::NumericOverflow, "Flex target size overflow", item.id, result.measurement_count);
                state.measured = measure(item.id, input, user_data);
                ++result.measurement_count;
                if (!measurement_ok(state.measured))
                    return failure(FlexErrorCode::InvalidMeasurement, "Invalid constrained measurement", item.id, result.measurement_count);
            }
            const double cross_min_content = measure_axis(state.measured.min_content, !axes.main_horizontal);
            const double cross_max_content = measure_axis(state.measured.max_content, !axes.main_horizontal);
            state.min_cross = resolve(dimension(item, !axes.main_horizontal, 1), axes.cross_extent,
                cross_min_content, cross_max_content, state.cross_bp, item.box_sizing).value_or(0);
            state.max_cross = resolve(dimension(item, !axes.main_horizontal, 2), axes.cross_extent,
                cross_min_content, cross_max_content, state.cross_bp, item.box_sizing).value_or(infinity);
            state.max_cross = std::max(state.min_cross, state.max_cross);
            const auto specified = resolve(dimension(item, !axes.main_horizontal, 0), axes.cross_extent,
                cross_min_content, cross_max_content, state.cross_bp, item.box_sizing);
            state.cross_auto = !specified;
            double desired = specified.value_or(measure_axis(state.measured.preferred, !axes.main_horizontal));
            if (!specified && item.aspect_ratio && item.visibility == FlexVisibility::Visible)
                desired = axes.main_horizontal ? state.target / *item.aspect_ratio
                    : state.target * *item.aspect_ratio;
            state.cross = std::clamp(desired, state.min_cross, state.max_cross);
            const double outer_cross = state.cross + state.cross_bp
                + state.margin_cross_start + state.margin_cross_end;
            line.cross = std::max(line.cross, outer_cross);
            if (is_baseline(resolved_align(spec, item)) && item.visibility == FlexVisibility::Visible) {
                const auto alignment = resolved_align(spec, item);
                const double physical_baseline = axes.main_horizontal
                    ? (alignment == FlexAlign::LastBaseline
                        ? state.measured.last_baseline.value_or(state.cross)
                        : state.measured.first_baseline.value_or(state.cross))
                    : state.cross;
                const double baseline = axes.cross_sign > 0
                    ? physical_baseline : state.cross - physical_baseline;
                state.baseline = state.cross_bp_start + baseline;
                line.baseline_ascent = std::max(line.baseline_ascent,
                    state.margin_cross_start + state.baseline);
                line.baseline_descent = std::max(line.baseline_descent,
                    outer_cross - state.margin_cross_start - state.baseline);
            }
        }
        line.cross = std::max(line.cross, line.baseline_ascent + line.baseline_descent);
        for (auto index : line.states)
            line.cross = std::max(line.cross, collapsed_struts[index]);
    }

    // A single flex line fills the definite cross size, even when items are smaller.
    if (lines.size() == 1) lines[0].cross = std::max(lines[0].cross, axes.cross_extent);
    const double natural_cross = std::accumulate(lines.begin(), lines.end(), 0.0,
        [](double sum, const Line& line) { return sum + line.cross; })
        + (lines.size() > 1 ? axes.cross_gap * static_cast<double>(lines.size() - 1) : 0);
    const double cross_free = axes.cross_extent - natural_cross;
    FlexAlign align_content = spec.align_content.value;
    if (align_content == FlexAlign::Normal) align_content = FlexAlign::Stretch;
    align_content = logical_alignment(align_content, spec.wrap == FlexWrap::WrapReverse);
    if (spec.align_content.safe && cross_free < 0) align_content = FlexAlign::FlexStart;
    if (align_content == FlexAlign::Stretch && cross_free > 0 && !lines.empty()) {
        for (auto& line : lines) line.cross += cross_free / static_cast<double>(lines.size());
    }
    double line_offset = 0, line_extra = 0;
    alignment_offset(align_content, cross_free, lines.size(), line_offset, line_extra);
    double cross_cursor = line_offset;
    for (auto& line : lines) {
        line.cross_pos = cross_cursor;
        cross_cursor += line.cross + axes.cross_gap + line_extra;
    }

    std::size_t paint_cursor = 0;
    for (std::size_t line_index = 0; line_index < lines.size(); ++line_index) {
        Line& line = lines[line_index];
        std::vector<std::size_t> visible;
        for (auto index : line.states)
            if (items[index].visibility == FlexVisibility::Visible) visible.push_back(index);
        const double base_gaps = visible.size() > 1
            ? axes.main_gap * static_cast<double>(visible.size() - 1) : 0;
        double occupied = base_gaps;
        std::size_t auto_count = 0;
        for (auto index : visible) {
            const State& state = states[index];
            occupied += state.target + state.main_bp
                + state.margin_main_start + state.margin_main_end;
            auto_count += static_cast<std::size_t>(state.main_auto_start)
                + static_cast<std::size_t>(state.main_auto_end);
        }
        const double free = axes.main_extent - occupied;
        const double auto_share = auto_count && free > 0
            ? free / static_cast<double>(auto_count) : 0;
        FlexAlign justify = spec.justify_content.value;
        if (justify == FlexAlign::Normal) justify = FlexAlign::FlexStart;
        justify = logical_alignment(justify,
            spec.direction == FlexDirection::RowReverse
            || spec.direction == FlexDirection::ColumnReverse);
        if (spec.justify_content.safe && free < 0) justify = FlexAlign::FlexStart;
        double offset = 0, extra = 0;
        if (auto_share == 0) alignment_offset(justify, free, visible.size(), offset, extra);
        double cursor = offset;
        for (auto index : visible) {
            State& state = states[index];
            const FlexItem& item = items[index];
            cursor += state.margin_main_start + (state.main_auto_start ? auto_share : 0);
            state.main_pos = cursor;
            cursor += state.target + state.main_bp + state.margin_main_end
                + (state.main_auto_end ? auto_share : 0) + axes.main_gap + extra;
            if (state.cross_auto && resolved_align(spec, item) == FlexAlign::Stretch
                && !state.cross_auto_start && !state.cross_auto_end) {
                state.cross = std::clamp(std::max(0.0, line.cross - state.cross_bp
                    - state.margin_cross_start - state.margin_cross_end),
                    state.min_cross, state.max_cross);
            }
            const double cross_used = state.cross + state.cross_bp
                + state.margin_cross_start + state.margin_cross_end;
            const double cross_remaining = line.cross - cross_used;
            const double cross_auto_share = cross_remaining > 0
                ? cross_remaining / (static_cast<int>(state.cross_auto_start)
                    + static_cast<int>(state.cross_auto_end) == 2 ? 2.0 : 1.0) : 0;
            FlexAlign alignment = logical_alignment(resolved_align(spec, item),
                spec.wrap == FlexWrap::WrapReverse);
            const bool safe = item.align_self.value == FlexAlign::Auto
                ? spec.align_items.safe : item.align_self.safe;
            if (safe && cross_remaining < 0) alignment = FlexAlign::FlexStart;
            double cross_offset = 0;
            if (!state.cross_auto_start && !state.cross_auto_end) {
                if (alignment == FlexAlign::Center) cross_offset = cross_remaining / 2;
                else if (alignment == FlexAlign::End || alignment == FlexAlign::FlexEnd)
                    cross_offset = cross_remaining;
                else if (is_baseline(alignment))
                    cross_offset = line.baseline_ascent - state.baseline - state.margin_cross_start;
            }
            state.cross_pos = line.cross_pos + state.margin_cross_start
                + (state.cross_auto_start ? cross_auto_share : 0) + cross_offset;
        }
        for (auto index : line.states) if (items[index].visibility == FlexVisibility::Collapse) {
            states[index].main_pos = 0;
            states[index].cross_pos = line.cross_pos;
        }
        Rect line_rect = axes.rect(0, line.cross_pos, axes.main_extent, line.cross);
        if (!unit_ok(line_rect.x) || !unit_ok(line_rect.y)
            || !size_ok(line_rect.width) || !size_ok(line_rect.height))
            return failure(FlexErrorCode::NumericOverflow, "Flex line geometry overflow", {}, result.measurement_count);
        result.lines.push_back({paint_cursor, line.states.size(), line_rect});
        paint_cursor += line.states.size();
    }

    // Absolute children are removed from flex sizing. Their static position is
    // computed as a sole flex item before optional physical inset adjustment.
    for (auto index : order) if (items[index].position == FlexPosition::Absolute) {
        State& state = states[index];
        const auto& item = items[index];
        state.target = state.hypo;
        const double main_outer = state.target + state.main_bp
            + state.margin_main_start + state.margin_main_end;
        const double cross_outer = state.cross + state.cross_bp
            + state.margin_cross_start + state.margin_cross_end;
        double main_offset = 0, unused = 0;
        FlexAlign justify = spec.justify_content.value == FlexAlign::Normal
            ? FlexAlign::FlexStart : spec.justify_content.value;
        justify = logical_alignment(justify,
            spec.direction == FlexDirection::RowReverse
            || spec.direction == FlexDirection::ColumnReverse);
        alignment_offset(justify, axes.main_extent - main_outer, 1, main_offset, unused);
        FlexAlign align = item.align_self.value == FlexAlign::Auto
            ? FlexAlign::Start : resolved_align(spec, item);
        align = logical_alignment(align, spec.wrap == FlexWrap::WrapReverse);
        double cross_offset = 0;
        if (align == FlexAlign::Center) cross_offset = (axes.cross_extent - cross_outer) / 2;
        else if (align == FlexAlign::End || align == FlexAlign::FlexEnd)
            cross_offset = axes.cross_extent - cross_outer;
        state.main_pos = main_offset + state.margin_main_start;
        state.cross_pos = cross_offset + state.margin_cross_start;
    }

    double min_x = 0, min_y = 0, max_x = available.width, max_y = available.height;
    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto& item = items[index];
        State& state = states[index];
        FlexItemLayout output;
        output.id = item.id;
        output.line_index = state.line;
        output.absolute = item.position == FlexPosition::Absolute;
        output.collapsed = item.visibility == FlexVisibility::Collapse;
        const double main_size = output.collapsed ? 0 : state.target + state.main_bp;
        const double cross_size = output.collapsed ? 0 : state.cross + state.cross_bp;
        output.rect = axes.rect(state.main_pos, state.cross_pos, main_size, cross_size);
        output.static_position_rect = output.rect;
        if (output.absolute) {
            // The static-position rectangle has the container's content edges
            // on the cross axis and the sole item's margin edges on the main.
            const double static_main_start = state.main_pos - state.margin_main_start;
            const double static_main_size = main_size
                + state.margin_main_start + state.margin_main_end;
            output.static_position_rect = axes.rect(static_main_start, 0,
                static_main_size, axes.cross_extent);
            const auto left = item.inset.left, right = item.inset.right;
            const auto top = item.inset.top, bottom = item.inset.bottom;
            const auto inset_value = [](FlexLength x, double basis) -> std::optional<double> {
                if (x.kind == FlexLength::Kind::Fixed) return x.value;
                if (x.kind == FlexLength::Kind::Percent) return x.value * basis;
                return std::nullopt;
            };
            const auto l = inset_value(left, axes.physical_width);
            const auto r = inset_value(right, axes.physical_width);
            const auto t = inset_value(top, axes.physical_height);
            const auto b = inset_value(bottom, axes.physical_height);
            const double cb_x = spec.border.left, cb_y = spec.border.top;
            const double cb_width = std::max(0.0, static_cast<double>(available.width)
                - spec.border.left - spec.border.right);
            const double cb_height = std::max(0.0, static_cast<double>(available.height)
                - spec.border.top - spec.border.bottom);
            const double margin_left = margin(item.margin.left, inline_extent);
            const double margin_right = margin(item.margin.right, inline_extent);
            const double margin_top = margin(item.margin.top, inline_extent);
            const double margin_bottom = margin(item.margin.bottom, inline_extent);
            if (l && r && item.width.kind == FlexLength::Kind::Auto) {
                const double content = std::max(0.0, cb_width - *l - *r
                    - margin_left - margin_right - item.border.left - item.border.right
                    - item.padding.left - item.padding.right);
                const double minimum = axes.main_horizontal ? state.min_main : state.min_cross;
                const double maximum = axes.main_horizontal ? state.max_main : state.max_cross;
                output.rect.width = static_cast<Unit>(std::clamp(content, minimum, maximum)
                    + item.border.left + item.border.right + item.padding.left + item.padding.right);
            }
            if (t && b && item.height.kind == FlexLength::Kind::Auto) {
                const double content = std::max(0.0, cb_height - *t - *b
                    - margin_top - margin_bottom - item.border.top - item.border.bottom
                    - item.padding.top - item.padding.bottom);
                const double minimum = axes.main_horizontal ? state.min_cross : state.min_main;
                const double maximum = axes.main_horizontal ? state.max_cross : state.max_main;
                output.rect.height = static_cast<Unit>(std::clamp(content, minimum, maximum)
                    + item.border.top + item.border.bottom + item.padding.top + item.padding.bottom);
            }
            if (l) output.rect.x = static_cast<Unit>(cb_x + *l + margin_left);
            else if (r) output.rect.x = static_cast<Unit>(cb_x + cb_width
                - *r - margin_right - output.rect.width);
            if (t) output.rect.y = static_cast<Unit>(cb_y + *t + margin_top);
            else if (b) output.rect.y = static_cast<Unit>(cb_y + cb_height
                - *b - margin_bottom - output.rect.height);
        }
        output.content_rect = {
            static_cast<Unit>(static_cast<double>(output.rect.x) + item.border.left + item.padding.left),
            static_cast<Unit>(static_cast<double>(output.rect.y) + item.border.top + item.padding.top),
            static_cast<Unit>(std::max(0.0, static_cast<double>(output.rect.width)
                - item.border.left - item.border.right - item.padding.left - item.padding.right)),
            static_cast<Unit>(std::max(0.0, static_cast<double>(output.rect.height)
                - item.border.top - item.border.bottom - item.padding.top - item.padding.bottom))
        };
        const auto rect_ok = [](Rect x) {
            return unit_ok(x.x) && unit_ok(x.y) && size_ok(x.width) && size_ok(x.height);
        };
        if (!rect_ok(output.rect) || !rect_ok(output.content_rect)
            || !rect_ok(output.static_position_rect))
            return failure(FlexErrorCode::NumericOverflow, "Final flex geometry overflow", item.id, result.measurement_count);
        min_x = std::min(min_x, static_cast<double>(output.rect.x));
        min_y = std::min(min_y, static_cast<double>(output.rect.y));
        max_x = std::max(max_x, static_cast<double>(output.rect.x) + output.rect.width);
        max_y = std::max(max_y, static_cast<double>(output.rect.y) + output.rect.height);
        result.items[index] = output;
    }
    if (!unit_ok(min_x) || !unit_ok(min_y)
        || !size_ok(max_x - min_x) || !size_ok(max_y - min_y))
        return failure(FlexErrorCode::NumericOverflow, "Flex occupied bounds overflow", {}, result.measurement_count);
    result.occupied_bounds = {static_cast<Unit>(min_x), static_cast<Unit>(min_y),
        static_cast<Unit>(max_x - min_x), static_cast<Unit>(max_y - min_y)};
    return result;
}
} // namespace axisly
