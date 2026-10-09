add_executable(axisly_css_lookup_bench css_lookup.cpp)
target_link_libraries(axisly_css_lookup_bench PRIVATE axisly::axisly)
set_target_properties(axisly_css_lookup_bench PROPERTIES CXX_EXTENSIONS OFF RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
axisly_enable_warnings(axisly_css_lookup_bench)

foreach(target IN ITEMS axisly_css_style_bench axisly_css_style_allocations)
    add_executable(${target} css_style_updates.cpp)
    target_link_libraries(${target} PRIVATE axisly::axisly)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    axisly_enable_warnings(${target})
endforeach()
target_sources(axisly_css_style_allocations PRIVATE allocation_probe.cpp)
target_compile_definitions(axisly_css_style_allocations PRIVATE AXISLY_COUNT_ALLOCATIONS=1)

foreach(target IN ITEMS axisly_css_retained_bench axisly_css_retained_allocations)
    add_executable(${target} css_retained.cpp css_text_metrics.cpp)
    target_link_libraries(${target} PRIVATE axisly::axisly)
    if(WIN32)
        target_link_libraries(${target} PRIVATE dwrite)
    endif()
    target_compile_definitions(${target} PRIVATE
        AXISLY_BENCH_VERSION="${PROJECT_VERSION}" AXISLY_BENCH_CONFIG="$<CONFIG>")
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    axisly_enable_warnings(${target})
endforeach()
target_sources(axisly_css_retained_allocations PRIVATE allocation_probe.cpp)
target_compile_definitions(axisly_css_retained_allocations PRIVATE AXISLY_COUNT_ALLOCATIONS=1)

foreach(target IN ITEMS axisly_css_grid_bench axisly_css_grid_allocations)
    add_executable(${target} css_grid.cpp)
    target_link_libraries(${target} PRIVATE axisly::axisly)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    axisly_enable_warnings(${target})
    if(AXISLY_ENABLE_GRID_METRICS)
        target_compile_definitions(${target} PRIVATE AXISLY_ENABLE_GRID_METRICS=1)
    endif()
endforeach()
target_sources(axisly_css_grid_allocations PRIVATE allocation_probe.cpp)
target_compile_definitions(axisly_css_grid_allocations PRIVATE AXISLY_COUNT_ALLOCATIONS=1)

if(AXISLY_BUILD_TESTS)
    add_test(NAME axisly.css_grid_bench.smoke COMMAND axisly_css_grid_bench --smoke)
    add_test(NAME axisly.css_grid_bench.allocations_smoke COMMAND axisly_css_grid_allocations --smoke)
    add_test(NAME axisly.css_style_bench.smoke COMMAND axisly_css_style_bench --smoke)
    add_test(NAME axisly.css_style_bench.allocations_smoke COMMAND axisly_css_style_allocations --smoke)
    add_test(NAME axisly.css_retained_bench.geometry_and_smoke
        COMMAND axisly_css_retained_bench --smoke)
    add_test(NAME axisly.css_retained_bench.allocations_smoke
        COMMAND axisly_css_retained_allocations --smoke)
    if(WIN32)
        add_test(NAME axisly.css_retained_bench.directwrite_smoke
            COMMAND axisly_css_retained_bench --backend directwrite --smoke)
        set_tests_properties(axisly.css_retained_bench.directwrite_smoke
            PROPERTIES LABELS benchmark TIMEOUT 120)
    endif()
    add_test(NAME axisly.css_retained_bench.reject_zero
        COMMAND axisly_css_retained_bench --iterations 0)
    set_tests_properties(axisly.css_retained_bench.reject_zero PROPERTIES WILL_FAIL TRUE)
    set_tests_properties(axisly.css_retained_bench.geometry_and_smoke
        axisly.css_retained_bench.allocations_smoke
        axisly.css_retained_bench.reject_zero PROPERTIES LABELS benchmark TIMEOUT 120)

endif()

if(AXISLY_BUILD_COMPARISON_BENCHMARKS)
    add_subdirectory(comparison)
endif()
