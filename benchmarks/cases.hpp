#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <gridly/gridly.hpp>

#include "../examples/nested_grid_adapter.hpp"

namespace gridly::bench {
struct NestedScene {
    examples::NestedGrid leaf;
    examples::NestedGrid panel;
    examples::NestedGrid root;
    NestedScene();
};
struct Case {
    std::string name;
    GridSpec spec;
    std::vector<GridItem> items;
    std::vector<Size> containers;
    bool measured{};
    std::unique_ptr<NestedScene> nested;
};
struct Observation {
    double checksum{};
    std::size_t measurements{};
    std::size_t layouts{};
};
[[nodiscard]] std::vector<Case> make_cases();
// Full geometry and repeatability checks run outside every measurement window.
void validate(const Case& scenario);
// Includes calculation, O(1) result consumption and result destruction.
[[nodiscard]] Observation run(const Case& scenario, std::size_t iteration);
} // namespace gridly::bench
