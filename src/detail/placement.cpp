#include "placement.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <utility>

namespace axisly::detail {
namespace {
using Index = std::uint64_t;
using Interval = std::pair<Index, Index>;

class Placement {
public:
    Placement(const GridSpec& source, std::span<const GridItem> input, PlacementWorkspace& output, GridError& error)
        : source_(source), output_(output), error_(error),
          column_flow_(source.auto_flow == AutoFlow::Column || source.auto_flow == AutoFlow::ColumnDense),
          dense_(source.auto_flow == AutoFlow::RowDense || source.auto_flow == AutoFlow::ColumnDense) {
        output.spec = source;
        output.items.assign(input.begin(), input.end());
        major_count_ = column_flow_ ? source.columns.size() : source.rows.size();
        minor_count_ = column_flow_ ? source.rows.size() : source.columns.size();
        initial_major_ = major_count_;
        initial_minor_ = minor_count_;
    }

    bool run() {
        switch (source_.auto_flow) {
        case AutoFlow::Explicit: case AutoFlow::Row: case AutoFlow::Column:
        case AutoFlow::RowDense: case AutoFlow::ColumnDense: break;
        default: return fail(GridErrorCode::InvalidPlacement, "Unknown automatic flow");
        }
        // Establish the minor-axis width before scanning rows (transpose for
        // column flow). Fully explicit items reserve space before automatic ones.
        for (auto& item : output_.items) {
            current_id_ = item.id;
            if (item.column_span == 0 || item.row_span == 0) return fail(GridErrorCode::InvalidSpan, "Spans must be positive");
            const bool auto_major = major(item) == auto_position;
            const bool auto_minor = minor(item) == auto_position;
            if ((auto_major || auto_minor) && source_.auto_flow == AutoFlow::Explicit)
                return fail(GridErrorCode::InvalidPlacement, "Automatic positions require an automatic flow");
            if (!grow(minor_count_, initial_minor_, auto_minor ? minor_span(item) : minor(item) + minor_span(item))) return false;
            if (!auto_major && !grow(major_count_, initial_major_, major(item) + major_span(item))) return false;
            if (!auto_major && !auto_minor && !occupy(item)) return false;
        }
        // Items locked to a major-axis position precede fully automatic items.
        for (auto& item : output_.items) {
            if (major(item) == auto_position || minor(item) != auto_position) continue;
            current_id_ = item.id;
            Index candidate = 0;
            if (!dense_) candidate = locked_cursor_[major(item)];
            for (;;) {
                if (!grow(minor_count_, initial_minor_, candidate + minor_span(item))) return false;
                Index next = candidate;
                if (!free(major(item), candidate, major_span(item), minor_span(item), next)) return false;
                if (next == candidate) break;
                candidate = next;
            }
            set_minor(item, candidate);
            // Sparse packing must follow earlier auto-placed items in every
            // row they span, not only the row at which those items start.
            for (Index row = major(item); !dense_ && row < major(item) + major_span(item); ++row) {
                if (!tick()) return false;
                auto& cursor = locked_cursor_[row];
                cursor = std::max(cursor, candidate + minor_span(item));
            }
            if (!occupy(item)) return false;
        }
        Index cursor_major = 0, cursor_minor = 0;
        for (auto& item : output_.items) {
            if (major(item) != auto_position) continue;
            current_id_ = item.id;
            const bool locked_minor = minor(item) != auto_position;
            Index row = dense_ ? 0 : cursor_major;
            Index column = dense_ ? 0 : cursor_minor;
            if (locked_minor) {
                if (!dense_ && minor(item) < column) ++row;
                column = minor(item);
            }
            for (;;) {
                if (!tick()) return false;
                if (!locked_minor && column + minor_span(item) > minor_count_) { ++row; column = 0; }
                if (!grow(major_count_, initial_major_, row + major_span(item))) return false;
                Index next = column;
                if (!free(row, column, major_span(item), minor_span(item), next)) return false;
                if (next == column) break;
                if (locked_minor) ++row;
                else column = next;
            }
            set_major(item, row);
            set_minor(item, column);
            cursor_major = row;
            cursor_minor = column + minor_span(item);
            if (!occupy(item)) return false;
        }
        auto& major_tracks = column_flow_ ? output_.spec.columns : output_.spec.rows;
        auto& minor_tracks = column_flow_ ? output_.spec.rows : output_.spec.columns;
        const auto& major_template = column_flow_ ? source_.implicit_column : source_.implicit_row;
        const auto& minor_template = column_flow_ ? source_.implicit_row : source_.implicit_column;
        major_tracks.resize(static_cast<std::size_t>(major_count_), major_template);
        minor_tracks.resize(static_cast<std::size_t>(minor_count_), minor_template);
        output_.prepared = true;
        return true;
    }

private:
    Index major(const GridItem& i) const { return column_flow_ ? i.column : i.row; }
    Index minor(const GridItem& i) const { return column_flow_ ? i.row : i.column; }
    Index major_span(const GridItem& i) const { return column_flow_ ? i.column_span : i.row_span; }
    Index minor_span(const GridItem& i) const { return column_flow_ ? i.row_span : i.column_span; }
    void set_major(GridItem& i, Index x) const { (column_flow_ ? i.column : i.row) = static_cast<std::uint32_t>(x); }
    void set_minor(GridItem& i, Index x) const { (column_flow_ ? i.row : i.column) = static_cast<std::uint32_t>(x); }
    bool fail(GridErrorCode code, const char* message) { error_ = {code, message, current_id_}; return false; }
    bool tick() {
        if (work_ >= source_.max_placement_work) return fail(GridErrorCode::PlacementLimit, "Placement work limit exceeded");
        ++work_;
        return true;
    }
    bool grow(Index& count, Index initial, Index required) {
        if (required <= count) return true;
        if (!source_.allow_implicit_tracks) return fail(GridErrorCode::InvalidPlacement, "Placement needs an implicit track");
        if (required > auto_position || required - initial > source_.max_implicit_tracks)
            return fail(GridErrorCode::TrackLimit, "Implicit track limit exceeded");
        count = required;
        return true;
    }
    bool free(Index row, Index column, Index rows, Index columns, Index& next) {
        if (!tick()) return false;
        auto found = occupied_.lower_bound(row);
        for (; found != occupied_.end() && found->first < row + rows; ++found) {
            if (!tick()) return false;
            const auto& intervals = found->second;
            const auto interval = std::lower_bound(intervals.begin(), intervals.end(), column,
                [](const Interval& i, Index x) { return i.second <= x; });
            if (interval != intervals.end() && interval->first < column + columns)
                next = std::max(next, interval->second);
        }
        return true;
    }
    bool occupy(const GridItem& item) {
        for (Index row = major(item); row < major(item) + major_span(item); ++row) {
            if (!tick()) return false;
            auto& intervals = occupied_[row];
            Interval merged{minor(item), minor(item) + minor_span(item)};
            auto first = std::lower_bound(intervals.begin(), intervals.end(), merged.first,
                [](const Interval& i, Index x) { return i.second < x; });
            auto last = first;
            while (last != intervals.end() && last->first <= merged.second) {
                if (!tick()) return false;
                merged.first = std::min(merged.first, last->first);
                merged.second = std::max(merged.second, last->second);
                ++last;
            }
            first = intervals.erase(first, last);
            intervals.insert(first, merged);
        }
        return true;
    }
    const GridSpec& source_;
    PlacementWorkspace& output_;
    GridError& error_;
    bool column_flow_, dense_;
    Index major_count_{}, minor_count_{}, initial_major_{}, initial_minor_{};
    std::size_t work_{};
    std::optional<ItemId> current_id_;
    std::map<Index, std::vector<Interval>> occupied_;
    std::map<Index, Index> locked_cursor_;
};
}

bool prepare_placement(const GridSpec& spec, std::span<const GridItem> items,
                       PlacementWorkspace& workspace, GridError& error) {
    workspace.prepared = false;
    return Placement{spec, items, workspace, error}.run();
}
}
