if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "Comparison benchmarks must run on the build host")
endif()
include(FetchContent)
FetchContent_Declare(gridly_bench_yoga
    URL https://codeload.github.com/facebook/yoga/tar.gz/refs/tags/v3.2.1
    URL_HASH SHA256=86b399ac31fd820d8ffa823c3fae31bb690b6fc45301b2a8a966c09b5a088b55
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR yoga)
FetchContent_MakeAvailable(gridly_bench_yoga)
set_target_properties(yogacore PROPERTIES INTERPROCEDURAL_OPTIMIZATION FALSE)
if(MSVC)
    target_compile_options(yogacore PRIVATE /utf-8)
endif()

add_executable(gridly_comparison_native native.cpp)
target_link_libraries(gridly_comparison_native PRIVATE gridly::gridly yogacore)
target_compile_definitions(gridly_comparison_native PRIVATE
    GRIDLY_BENCH_CONFIG="$<CONFIG>" GRIDLY_BENCH_VERSION="${PROJECT_VERSION}")
set_target_properties(gridly_comparison_native PROPERTIES CXX_EXTENSIONS OFF
    INTERPROCEDURAL_OPTIMIZATION FALSE RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
gridly_enable_warnings(gridly_comparison_native)
configure_file("${gridly_bench_yoga_SOURCE_DIR}/LICENSE" "${CMAKE_BINARY_DIR}/comparison-yoga-LICENSE.txt" COPYONLY)

find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
find_program(GRIDLY_COMPARISON_CARGO cargo REQUIRED)
find_program(GRIDLY_COMPARISON_RUSTC rustc REQUIRED)
execute_process(COMMAND "${GRIDLY_COMPARISON_RUSTC}" -vV OUTPUT_VARIABLE rust_version
    COMMAND_ERROR_IS_FATAL ANY)
string(REGEX MATCH "host: ([A-Za-z0-9_-]+)" rust_host_match "${rust_version}")
if(NOT rust_host_match)
    message(FATAL_ERROR "Cannot determine Rust host")
endif()
set(rust_host "${CMAKE_MATCH_1}")
set(taffy_exe "${CMAKE_CURRENT_BINARY_DIR}/cargo/${rust_host}/release/gridly-taffy-bench${CMAKE_EXECUTABLE_SUFFIX}")
add_custom_command(OUTPUT "${taffy_exe}"
    COMMAND "${GRIDLY_COMPARISON_CARGO}" build --release --locked
        --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/taffy/Cargo.toml" --target "${rust_host}"
        --target-dir "${CMAKE_CURRENT_BINARY_DIR}/cargo"
    DEPENDS taffy/Cargo.toml taffy/Cargo.lock taffy/src/main.rs
    VERBATIM)
add_custom_target(gridly_comparison_taffy ALL DEPENDS "${taffy_exe}")
if(GRIDLY_BUILD_TESTS)
    add_test(NAME gridly.comparison.geometry_and_smoke COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/compare.py" --native "$<TARGET_FILE:gridly_comparison_native>"
        --taffy "${taffy_exe}" --output "${CMAKE_CURRENT_BINARY_DIR}/smoke" --smoke)
    set_tests_properties(gridly.comparison.geometry_and_smoke PROPERTIES LABELS comparison TIMEOUT 120)
endif()
