if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "Comparison benchmarks must run on the build host")
endif()
include(FetchContent)
FetchContent_Declare(axisly_bench_yoga
    URL https://codeload.github.com/facebook/yoga/tar.gz/refs/tags/v3.2.1
    URL_HASH SHA256=86b399ac31fd820d8ffa823c3fae31bb690b6fc45301b2a8a966c09b5a088b55
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR yoga)
FetchContent_MakeAvailable(axisly_bench_yoga)
set_target_properties(yogacore PROPERTIES INTERPROCEDURAL_OPTIMIZATION FALSE)
if(MSVC)
    target_compile_options(yogacore PRIVATE /utf-8)
endif()

add_executable(axisly_comparison_native native.cpp)
target_link_libraries(axisly_comparison_native PRIVATE axisly::axisly yogacore)
target_compile_definitions(axisly_comparison_native PRIVATE
    AXISLY_BENCH_CONFIG="$<CONFIG>" AXISLY_BENCH_VERSION="${PROJECT_VERSION}")
set_target_properties(axisly_comparison_native PROPERTIES CXX_EXTENSIONS OFF
    INTERPROCEDURAL_OPTIMIZATION FALSE RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
axisly_enable_warnings(axisly_comparison_native)
configure_file("${axisly_bench_yoga_SOURCE_DIR}/LICENSE" "${CMAKE_BINARY_DIR}/comparison-yoga-LICENSE.txt" COPYONLY)

add_executable(axisly_css_comparison_native css_native.cpp)
target_link_libraries(axisly_css_comparison_native PRIVATE axisly::axisly yogacore)
target_compile_definitions(axisly_css_comparison_native PRIVATE
    AXISLY_BENCH_CONFIG="$<CONFIG>" AXISLY_BENCH_VERSION="${PROJECT_VERSION}")
set_target_properties(axisly_css_comparison_native PROPERTIES CXX_EXTENSIONS OFF
    INTERPROCEDURAL_OPTIMIZATION FALSE RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
axisly_enable_warnings(axisly_css_comparison_native)

add_executable(axisly_css_batch_native css_batch_native.cpp)
target_link_libraries(axisly_css_batch_native PRIVATE axisly::axisly)
set_target_properties(axisly_css_batch_native PROPERTIES CXX_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
axisly_enable_warnings(axisly_css_batch_native)

find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
find_program(AXISLY_COMPARISON_CARGO cargo REQUIRED)
find_program(AXISLY_COMPARISON_RUSTC rustc REQUIRED)
execute_process(COMMAND "${AXISLY_COMPARISON_RUSTC}" -vV OUTPUT_VARIABLE rust_version
    COMMAND_ERROR_IS_FATAL ANY)
string(REGEX MATCH "host: ([A-Za-z0-9_-]+)" rust_host_match "${rust_version}")
if(NOT rust_host_match)
    message(FATAL_ERROR "Cannot determine Rust host")
endif()
set(rust_host "${CMAKE_MATCH_1}")
set(taffy_exe "${CMAKE_CURRENT_BINARY_DIR}/cargo/${rust_host}/release/axisly-taffy-bench${CMAKE_EXECUTABLE_SUFFIX}")
set(taffy_css_exe "${CMAKE_CURRENT_BINARY_DIR}/cargo/${rust_host}/release/css${CMAKE_EXECUTABLE_SUFFIX}")
add_custom_command(OUTPUT "${taffy_exe}" "${taffy_css_exe}"
    COMMAND "${AXISLY_COMPARISON_CARGO}" build --release --locked
        --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/taffy/Cargo.toml" --target "${rust_host}"
        --target-dir "${CMAKE_CURRENT_BINARY_DIR}/cargo"
    DEPENDS taffy/Cargo.toml taffy/Cargo.lock taffy/src/main.rs taffy/src/bin/css.rs
    VERBATIM)
add_custom_target(axisly_comparison_taffy ALL DEPENDS "${taffy_exe}" "${taffy_css_exe}")
if(AXISLY_BUILD_TESTS)
    add_test(NAME axisly.comparison.geometry_and_smoke COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/compare.py" --native "$<TARGET_FILE:axisly_comparison_native>"
        --taffy "${taffy_exe}" --output "${CMAKE_CURRENT_BINARY_DIR}/smoke" --smoke)
    set_tests_properties(axisly.comparison.geometry_and_smoke PROPERTIES LABELS comparison TIMEOUT 120)
    add_test(NAME axisly.css_comparison.geometry_and_smoke COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/css_compare.py" --native "$<TARGET_FILE:axisly_css_comparison_native>"
        --taffy "${taffy_css_exe}" --output "${CMAKE_CURRENT_BINARY_DIR}/css-smoke" --smoke
        --case flex_fixed_100 --case flex_shrink_100 --case flex_wrap_100
        --case flex_nested_text_96 --case flex_deep_12 --case block_text_96
        --case grid_grow_20x20 --case mixed_text_96)
    set_tests_properties(axisly.css_comparison.geometry_and_smoke PROPERTIES LABELS comparison TIMEOUT 180)
    add_test(NAME axisly.css_comparison.build_and_layout COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/css_compare.py" --native "$<TARGET_FILE:axisly_css_comparison_native>"
        --taffy "${taffy_css_exe}" --output "${CMAKE_CURRENT_BINARY_DIR}/css-build-smoke" --smoke
        --mode build --case flex_fixed_100 --case mixed_text_96)
    set_tests_properties(axisly.css_comparison.build_and_layout PROPERTIES LABELS comparison TIMEOUT 120)
endif()
