cmake_minimum_required(VERSION 3.25)
project(gridly_consumer LANGUAGES CXX)

if(GRIDLY_SOURCE_DIR)
    add_subdirectory("${GRIDLY_SOURCE_DIR}" gridly-build)
    if(TARGET gridly_contract_tests OR TARGET gridly_fixed_tests OR TARGET gridly_weighted_tests OR TARGET gridly_alignment_tests
        OR TARGET gridly_reference_native OR TARGET gridly_taffy_reference
        OR TARGET gridly_playground OR TARGET gridly_playground_scenes OR TARGET SDL3::SDL3 OR TARGET SDL3_ttf::SDL3_ttf
        OR TARGET gridly_bench OR TARGET gridly_bench_allocations OR TARGET gridly_bench_cases OR TARGET gridly_cache_allocations
        OR TARGET gridly_comparison_native OR TARGET gridly_comparison_taffy OR TARGET yogacore
        OR TARGET gridly_measurement_tests OR TARGET gridly_spanning_tests OR TARGET gridly_range_tests OR TARGET gridly_nested_tests OR TARGET gridly_cache_tests
        OR TARGET gridly_advanced_tests OR TARGET gridly_tree_tests OR TARGET gridly_retained_example
        OR TARGET gridly_api_example OR TARGET gridly_weighted_example OR TARGET gridly_alignment_example
        OR TARGET gridly_measurement_example OR TARGET gridly_spanning_example OR TARGET gridly_range_example OR TARGET gridly_nested_example)
        message(FATAL_ERROR "gridly must not build its own tests or examples by default when embedded")
    endif()
else()
    find_package(gridly CONFIG REQUIRED)
endif()

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE gridly::gridly)

enable_testing()
add_test(NAME gridly.consumer COMMAND consumer)
