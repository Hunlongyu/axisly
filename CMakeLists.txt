cmake_minimum_required(VERSION 3.25)

project(axisly VERSION 0.1.0 LANGUAGES CXX)

include(cmake/DetectMsvcIncludes.cmake)
axisly_detect_msvc_includes()

include(GNUInstallDirs)

option(AXISLY_BUILD_TESTS "Build the axisly contract tests" ${PROJECT_IS_TOP_LEVEL})
option(AXISLY_ENABLE_SANITIZERS "Instrument development builds with ASan (and UBSan on GCC/Clang Unix)" OFF)
if(AXISLY_ENABLE_SANITIZERS AND MSVC)
    # /RTC is incompatible with AddressSanitizer. Retain normal Debug behavior
    # when the explicit sanitizer option is off.
    string(REGEX REPLACE "/RTC[1su]+" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()
option(AXISLY_BUILD_EXAMPLES "Build the axisly API example" ${PROJECT_IS_TOP_LEVEL})
option(AXISLY_BUILD_REFERENCE_TESTS "Build developer-only Taffy differential tooling (requires Cargo and Python)" OFF)
option(AXISLY_BUILD_PLAYGROUND "Build the optional SDL3 visual playground" OFF)
option(AXISLY_BUILD_GEEYOOU_SHOWCASE "Build the optional Windows GeeyoouUI showcase (downloads pinned sources)" OFF)
option(AXISLY_FETCH_SDL3 "Download pinned SDL3 sources for the playground" OFF)
option(AXISLY_BUILD_BENCHMARKS "Build developer-only layout timing and allocation benchmarks" OFF)
option(AXISLY_BUILD_COMPARISON_BENCHMARKS "Build opt-in Taffy/Yoga benchmarks (downloads Yoga; requires Cargo/Python)" OFF)
if(AXISLY_BUILD_COMPARISON_BENCHMARKS AND NOT AXISLY_BUILD_BENCHMARKS)
    message(FATAL_ERROR "AXISLY_BUILD_COMPARISON_BENCHMARKS requires AXISLY_BUILD_BENCHMARKS=ON")
endif()
if(AXISLY_BUILD_REFERENCE_TESTS AND NOT AXISLY_BUILD_TESTS)
    message(FATAL_ERROR "AXISLY_BUILD_REFERENCE_TESTS requires AXISLY_BUILD_TESTS=ON")
endif()

add_library(axisly STATIC
    src/css_box.cpp
    src/css_grid.cpp
    src/css_measurement.cpp
    src/css_style.cpp
    src/css_tree.cpp
    src/css/layout_engine.cpp
    src/css/block_layout.cpp
    src/css/block_intrinsic_width.cpp
    src/css/block_float_context.cpp
    src/css/flex_layout.cpp
    src/css/grid_layout.cpp
    src/css/grid_item_sizing.cpp
    src/css/grid_track_sizing.cpp
    src/css/positioned_layout.cpp
    src/css/overflow_layout.cpp
    src/css/balanced_lines.cpp
    src/css_values.cpp
    src/track.cpp
    src/compute_grid.cpp
    src/cache.cpp
    src/rounding.cpp
    src/measurement_cache.cpp
    src/tree.cpp
    src/detail/placement.cpp
    src/detail/track_sizing.cpp
    src/detail/track_state.cpp
    src/detail/span_resolution.cpp
    src/detail/item_alignment.cpp
)
add_library(axisly::axisly ALIAS axisly)
set_target_properties(axisly PROPERTIES
    EXPORT_NAME axisly
    CXX_EXTENSIONS OFF
)
target_compile_features(axisly PUBLIC cxx_std_20)
target_include_directories(axisly PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

function(axisly_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()
    if(AXISLY_ENABLE_SANITIZERS)
        if(MSVC)
            target_compile_options(${target} PRIVATE /fsanitize=address /Zi)
            target_link_options(${target} PRIVATE /INCREMENTAL:NO)
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        else()
            message(FATAL_ERROR "AXISLY_ENABLE_SANITIZERS requires MSVC, Clang or GCC")
        endif()
    endif()
endfunction()
axisly_enable_warnings(axisly)
if(AXISLY_ENABLE_SANITIZERS AND NOT MSVC)
    target_link_options(axisly INTERFACE -fsanitize=address,undefined)
elseif(AXISLY_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # CMake invokes lld-link directly, bypassing clang-cl's ASan driver linkage.
    # Discover the compiler's own runtimes, never a hard-coded LLVM version.
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR CMAKE_SYSTEM_PROCESSOR MATCHES "ARM|arm|aarch")
        message(FATAL_ERROR "The Windows clang-cl sanitizer preset currently requires x64")
    endif()
    if(CMAKE_MSVC_RUNTIME_LIBRARY AND NOT CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL")
        message(FATAL_ERROR "Windows clang-cl sanitizers require the dynamic CRT (/MD or /MDd)")
    endif()
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -print-resource-dir
        OUTPUT_VARIABLE axisly_clang_resource OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    set(axisly_asan_dir "${axisly_clang_resource}/lib/windows")
    set(axisly_asan_runtime "${axisly_asan_dir}/clang_rt.asan_dynamic-x86_64.lib")
    set(axisly_asan_thunk "${axisly_asan_dir}/clang_rt.asan_dynamic_runtime_thunk-x86_64.lib")
    if(NOT EXISTS "${axisly_asan_runtime}" OR NOT EXISTS "${axisly_asan_thunk}")
        message(FATAL_ERROR "Clang AddressSanitizer runtimes are missing from ${axisly_asan_dir}")
    endif()
    target_link_libraries(axisly INTERFACE "${axisly_asan_runtime}")
    target_link_options(axisly INTERFACE "/wholearchive:${axisly_asan_thunk}" /include:__asan_seh_interceptor)
endif()

if(AXISLY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(AXISLY_BUILD_EXAMPLES)
    add_executable(axisly_api_example examples/fixed_grid.cpp)
    target_link_libraries(axisly_api_example PRIVATE axisly::axisly)
    set_target_properties(axisly_api_example PROPERTIES CXX_EXTENSIONS OFF)
    axisly_enable_warnings(axisly_api_example)

    add_executable(axisly_weighted_example examples/weighted_grid.cpp)
    target_link_libraries(axisly_weighted_example PRIVATE axisly::axisly)
    set_target_properties(axisly_weighted_example PROPERTIES CXX_EXTENSIONS OFF)
    axisly_enable_warnings(axisly_weighted_example)

    add_executable(axisly_alignment_example examples/alignment_grid.cpp)
    target_link_libraries(axisly_alignment_example PRIVATE axisly::axisly)
    set_target_properties(axisly_alignment_example PROPERTIES CXX_EXTENSIONS OFF)
    axisly_enable_warnings(axisly_alignment_example)

    add_executable(axisly_measurement_example examples/measurement_grid.cpp)
    target_link_libraries(axisly_measurement_example PRIVATE axisly::axisly)
    set_target_properties(axisly_measurement_example PROPERTIES CXX_EXTENSIONS OFF)
    axisly_enable_warnings(axisly_measurement_example)

    add_executable(axisly_spanning_example examples/spanning_grid.cpp)
    target_link_libraries(axisly_spanning_example PRIVATE axisly::axisly)
    set_target_properties(axisly_spanning_example PROPERTIES CXX_EXTENSIONS OFF)
    axisly_enable_warnings(axisly_spanning_example)

    add_executable(axisly_range_example examples/range_grid.cpp)
    target_link_libraries(axisly_range_example PRIVATE axisly::axisly)
    set_target_properties(axisly_range_example PROPERTIES CXX_EXTENSIONS OFF)
    axisly_enable_warnings(axisly_range_example)

    add_executable(axisly_nested_example examples/nested_grid.cpp)
    target_link_libraries(axisly_nested_example PRIVATE axisly::axisly)
    set_target_properties(axisly_nested_example PROPERTIES CXX_EXTENSIONS OFF)
    axisly_enable_warnings(axisly_nested_example)

    add_executable(axisly_retained_example examples/retained_grid.cpp)
    target_link_libraries(axisly_retained_example PRIVATE axisly::axisly)
    set_target_properties(axisly_retained_example PROPERTIES CXX_EXTENSIONS OFF)
    axisly_enable_warnings(axisly_retained_example)
    if(AXISLY_BUILD_TESTS)
        add_test(NAME axisly.retained.example COMMAND axisly_retained_example)
    endif()
endif()

if(AXISLY_BUILD_PLAYGROUND)
    add_subdirectory(examples/playground)
endif()

if(AXISLY_BUILD_GEEYOOU_SHOWCASE)
    add_subdirectory(examples/geeyoou)
endif()

if(AXISLY_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

include(CMakePackageConfigHelpers)

install(TARGETS axisly EXPORT axislyTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT axislyTargets
    FILE axislyTargets.cmake
    NAMESPACE axisly::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/axisly
)
configure_package_config_file(cmake/axislyConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/axislyConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/axisly
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/axislyConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY ExactVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/axislyConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/axislyConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/axisly
)
