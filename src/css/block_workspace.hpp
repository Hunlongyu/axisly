#pragma once

#include "layout_fragment.hpp"
#include "workspace_pool.hpp"
#include "block_float_context.hpp"

namespace axisly::css::detail {

struct BlockWorkspace {
    explicit BlockWorkspace(std::pmr::memory_resource* memory)
        : fragments(memory), static_block_y(memory), floats(memory) {}
    std::pmr::vector<Fragment> fragments;
    std::pmr::vector<std::optional<Unit>> static_block_y;
    FloatContext floats;
    void clear() noexcept {
        fragments.clear();
        static_block_y.clear();
        floats.clear();
    }
};

} // namespace axisly::css::detail
