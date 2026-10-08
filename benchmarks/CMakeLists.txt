add_library(axisly_bench_cases STATIC cases.cpp)
target_link_libraries(axisly_bench_cases PUBLIC axisly::axisly)
target_include_directories(axisly_bench_cases PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(axisly_bench_cases PROPERTIES CXX_EXTENSIONS OFF)
axisly_enable_warnings(axisly_bench_cases)

foreach(target IN ITEMS axisly_bench axisly_bench_allocations)
    add_executable(${target} main.cpp)
    target_link_libraries(${target} PRIVATE axisly_bench_cases)
    target_compile_definitions(${target} PRIVATE
        AXISLY_BENCH_VERSION="${PROJECT_VERSION}"
        AXISLY_BENCH_CONFIG="$<CONFIG>"
    )
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    axisly_enable_warnings(${target})
endforeach()

# The timed executable uses the normal allocator. Replacement new/delete exist
# ONLY in the second executable, never in the core or consumer link interface.
target_sources(axisly_bench_allocations PRIVATE allocation_probe.cpp)
target_compile_definitions(axisly_bench_allocations PRIVATE AXISLY_COUNT_ALLOCATIONS=1)

add_executable(axisly_css_lookup_bench css_lookup.cpp)
target_link_libraries(axisly_css_lookup_bench PRIVATE axisly::axisly)
set_target_properties(axisly_css_lookup_bench PROPERTIES CXX_EXTENSIONS OFF RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
axisly_enable_warnings(axisly_css_lookup_bench)

add_executable(axisly_cache_allocations cache_allocations.cpp allocation_probe.cpp)
target_link_libraries(axisly_cache_allocations PRIVATE axisly::axisly)
set_target_properties(axisly_cache_allocations PROPERTIES CXX_EXTENSIONS OFF RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
axisly_enable_warnings(axisly_cache_allocations)

add_executable(axisly_retained_bench retained.cpp)
target_link_libraries(axisly_retained_bench PRIVATE axisly::axisly)
if(WIN32)
    target_link_libraries(axisly_retained_bench PRIVATE dwrite)
endif()
target_compile_definitions(axisly_retained_bench PRIVATE
    AXISLY_BENCH_VERSION="${PROJECT_VERSION}" AXISLY_BENCH_CONFIG="$<CONFIG>")
set_target_properties(axisly_retained_bench PROPERTIES CXX_EXTENSIONS OFF RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
axisly_enable_warnings(axisly_retained_bench)

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

if(AXISLY_BUILD_TESTS)
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
    add_test(NAME axisly.retained_bench.geometry_and_smoke COMMAND axisly_retained_bench --smoke)
    add_test(NAME axisly.retained_bench.reject_zero COMMAND axisly_retained_bench --iterations 0)
    add_test(NAME axisly.retained_bench.reject_unknown COMMAND axisly_retained_bench --invalid)
    set_tests_properties(axisly.retained_bench.reject_zero axisly.retained_bench.reject_unknown PROPERTIES WILL_FAIL TRUE)
    set_tests_properties(axisly.retained_bench.geometry_and_smoke axisly.retained_bench.reject_zero
        axisly.retained_bench.reject_unknown PROPERTIES LABELS benchmark TIMEOUT 120)
    add_test(NAME axisly.cache.no_allocations COMMAND axisly_cache_allocations)
    set_tests_properties(axisly.cache.no_allocations PROPERTIES LABELS benchmark TIMEOUT 60)
    add_test(NAME axisly.bench.timing_smoke COMMAND axisly_bench --smoke)
    add_test(NAME axisly.bench.allocations_smoke COMMAND axisly_bench_allocations --smoke)
    add_test(NAME axisly.bench.counter_contract COMMAND axisly_bench_allocations --self-test)
    add_test(NAME axisly.bench.reject_zero COMMAND axisly_bench --iterations 0)
    add_test(NAME axisly.bench.reject_unknown COMMAND axisly_bench --case missing)
    set_tests_properties(axisly.bench.reject_zero axisly.bench.reject_unknown PROPERTIES WILL_FAIL TRUE)
    set_tests_properties(axisly.bench.timing_smoke axisly.bench.allocations_smoke axisly.bench.counter_contract
        axisly.bench.reject_zero axisly.bench.reject_unknown PROPERTIES LABELS benchmark TIMEOUT 60)
endif()

if(AXISLY_BUILD_COMPARISON_BENCHMARKS)
    add_subdirectory(comparison)
endif()
