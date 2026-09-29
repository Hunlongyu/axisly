cmake_minimum_required(VERSION 3.25)
project(axisly_consumer LANGUAGES CXX)

if(AXISLY_SOURCE_DIR)
    add_subdirectory("${AXISLY_SOURCE_DIR}" axisly-build)
    if(TARGET axisly_contract_tests OR TARGET axisly_fixed_tests OR TARGET axisly_weighted_tests OR TARGET axisly_alignment_tests
        OR TARGET axisly_reference_native OR TARGET axisly_taffy_reference
        OR TARGET axisly_playground OR TARGET axisly_playground_scenes OR TARGET SDL3::SDL3 OR TARGET SDL3_ttf::SDL3_ttf
        OR TARGET axisly_bench OR TARGET axisly_bench_allocations OR TARGET axisly_bench_cases OR TARGET axisly_cache_allocations
        OR TARGET axisly_css_retained_bench OR TARGET axisly_css_retained_allocations
        OR TARGET axisly_comparison_native OR TARGET axisly_comparison_taffy OR TARGET yogacore
        OR TARGET axisly_measurement_tests OR TARGET axisly_spanning_tests OR TARGET axisly_range_tests OR TARGET axisly_nested_tests OR TARGET axisly_cache_tests
        OR TARGET axisly_advanced_tests OR TARGET axisly_tree_tests OR TARGET axisly_retained_example
        OR TARGET axisly_api_example OR TARGET axisly_weighted_example OR TARGET axisly_alignment_example
        OR TARGET axisly_measurement_example OR TARGET axisly_spanning_example OR TARGET axisly_range_example OR TARGET axisly_nested_example)
        message(FATAL_ERROR "axisly must not build its own tests or examples by default when embedded")
    endif()
else()
    find_package(axisly CONFIG REQUIRED)
endif()

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE axisly::axisly)

enable_testing()
add_test(NAME axisly.consumer COMMAND consumer)
