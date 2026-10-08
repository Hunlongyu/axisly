#include "../../src/css/balanced_lines.hpp"
#include "../../benchmarks/allocation_probe.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        using namespace axisly::css::detail;
        BalancedLineWorkspace workspace(std::pmr::new_delete_resource());
        const std::array<axisly::Unit, 6> sizes{40, 40, 40, 40, 40, 40};
        const auto run = [&] {
            const auto counts = balanced_line_counts(sizes, 130, 4, 2, 1000, workspace);
            if (counts.size() != 2 || counts[0] != 3 || counts[1] != 3)
                throw std::runtime_error("Unexpected balanced division");
        };
        run();
        axisly::bench::begin_allocations();
        for (int i = 0; i < 20; ++i) run();
        const auto allocations = axisly::bench::end_allocations();
        if (allocations.allocations != 0) throw std::runtime_error("Warm balance allocated");
        bool limited = false;
        try { (void)balanced_line_counts(sizes, 130, 4, 2, 0, workspace); }
        catch (const std::length_error&) { limited = true; }
        if (!limited) throw std::runtime_error("Balance work limit ignored");
        run(); // Partially filled workspace from the failure cannot affect reuse.
        const auto single = balanced_line_counts(sizes, 1, 0, 1, 1000, workspace);
        if (single.size() != sizes.size()) throw std::runtime_error("Oversized item division");
        for (const auto count : single) if (count != 1) throw std::runtime_error("Stale counts");
        if (!balanced_line_counts({}, 130, 4, 2, 1000, workspace).empty())
            throw std::runtime_error("Empty input retained prior counts");
        run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
