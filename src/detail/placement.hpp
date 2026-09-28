#pragma once
#include <axisly/result.hpp>
#include <span>

namespace axisly::detail {
struct PlacementWorkspace {
    GridSpec spec;
    std::vector<GridItem> items;
    bool prepared{};
};
// Produces explicit indices and materializes implicit tracks without changing
// caller inputs. Explicit overlaps remain legal; automatic items avoid them.
bool prepare_placement(const GridSpec& spec, std::span<const GridItem> items,
                       PlacementWorkspace& workspace, GridError& error);
}
