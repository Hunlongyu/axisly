add_library(gridly_bench_cases STATIC cases.cpp)
target_link_libraries(gridly_bench_cases PUBLIC gridly::gridly)
target_include_directories(gridly_bench_cases PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(gridly_bench_cases PROPERTIES CXX_EXTENSIONS OFF)
gridly_enable_warnings(gridly_bench_cases)

foreach(target IN ITEMS gridly_bench gridly_bench_allocations)
    add_executable(${target} main.cpp)
    target_link_libraries(${target} PRIVATE gridly_bench_cases)
    target_compile_definitions(${target} PRIVATE
        GRIDLY_BENCH_VERSION="${PROJECT_VERSION}"
        GRIDLY_BENCH_CONFIG="$<CONFIG>"
    )
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    gridly_enable_warnings(${target})
endforeach()

# The timed executable uses the normal allocator. Replacement new/delete exist
# ONLY in the second executable, never in the core or consumer link interface.
target_sources(gridly_bench_allocations PRIVATE allocation_probe.cpp)
target_compile_definitions(gridly_bench_allocations PRIVATE GRIDLY_COUNT_ALLOCATIONS=1)

add_executable(gridly_cache_allocations cache_allocations.cpp allocation_probe.cpp)
target_link_libraries(gridly_cache_allocations PRIVATE gridly::gridly)
set_target_properties(gridly_cache_allocations PROPERTIES CXX_EXTENSIONS OFF RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
gridly_enable_warnings(gridly_cache_allocations)

if(GRIDLY_BUILD_TESTS)
    add_test(NAME gridly.cache.no_allocations COMMAND gridly_cache_allocations)
    set_tests_properties(gridly.cache.no_allocations PROPERTIES LABELS benchmark TIMEOUT 60)
    add_test(NAME gridly.bench.timing_smoke COMMAND gridly_bench --smoke)
    add_test(NAME gridly.bench.allocations_smoke COMMAND gridly_bench_allocations --smoke)
    add_test(NAME gridly.bench.counter_contract COMMAND gridly_bench_allocations --self-test)
    add_test(NAME gridly.bench.reject_zero COMMAND gridly_bench --iterations 0)
    add_test(NAME gridly.bench.reject_unknown COMMAND gridly_bench --case missing)
    set_tests_properties(gridly.bench.reject_zero gridly.bench.reject_unknown PROPERTIES WILL_FAIL TRUE)
    set_tests_properties(gridly.bench.timing_smoke gridly.bench.allocations_smoke gridly.bench.counter_contract
        gridly.bench.reject_zero gridly.bench.reject_unknown PROPERTIES LABELS benchmark TIMEOUT 60)
endif()

if(GRIDLY_BUILD_COMPARISON_BENCHMARKS)
    add_subdirectory(comparison)
endif()
