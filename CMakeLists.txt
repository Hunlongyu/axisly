cmake_minimum_required(VERSION 3.25)

project(axisly VERSION 0.2.0 LANGUAGES CXX)

include(cmake/DetectMsvcIncludes.cmake)
axisly_detect_msvc_includes()

include(GNUInstallDirs)

option(AXISLY_BUILD_TESTS "Build the axisly contract tests" ${PROJECT_IS_TOP_LEVEL})
option(AXISLY_ENABLE_SANITIZERS "Instrument development builds with ASan (and UBSan on GCC/Clang Unix)" OFF)
option(AXISLY_ENABLE_TSAN "Instrument Linux development builds with ThreadSanitizer" OFF)
option(AXISLY_ENABLE_COVERAGE "Instrument Clang development builds with LLVM source coverage" OFF)
if(AXISLY_ENABLE_TSAN AND (AXISLY_ENABLE_SANITIZERS OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    message(FATAL_ERROR "ThreadSanitizer requires a separate Linux build without ASan/UBSan")
endif()
if(AXISLY_ENABLE_TSAN AND NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "ThreadSanitizer requires GCC or Clang")
endif()
if(AXISLY_ENABLE_COVERAGE AND (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR AXISLY_ENABLE_SANITIZERS OR AXISLY_ENABLE_TSAN))
    message(FATAL_ERROR "LLVM coverage requires a separate Clang build without sanitizers")
endif()
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
option(AXISLY_ENABLE_GRID_METRICS "Count Grid work in development builds (not for timing)" OFF)
option(AXISLY_BUILD_COMPARISON_BENCHMARKS "Build opt-in Taffy/Yoga benchmarks (downloads Yoga; requires Cargo/Python)" OFF)
if(AXISLY_BUILD_COMPARISON_BENCHMARKS AND NOT AXISLY_BUILD_BENCHMARKS)
    message(FATAL_ERROR "AXISLY_BUILD_COMPARISON_BENCHMARKS requires AXISLY_BUILD_BENCHMARKS=ON")
endif()
if(AXISLY_BUILD_REFERENCE_TESTS AND NOT AXISLY_BUILD_TESTS)
    message(FATAL_ERROR "AXISLY_BUILD_REFERENCE_TESTS requires AXISLY_BUILD_TESTS=ON")
endif()

add_library(axisly STATIC
    src/css_box.cpp
    src/css_batch.cpp
    src/css_grid.cpp
    src/css_measurement.cpp
    src/css_memory.cpp
    src/css_style.cpp
    src/css_tree.cpp
    src/css/layout_engine.cpp
    src/css/layout_source.cpp
    src/css/layout_cache.cpp
    src/css/layout_fragment.cpp
    src/css/layout_rounding.cpp
    src/css/leaf_layout.cpp
    src/css/block_layout.cpp
    src/css/block_item_sizing.cpp
    src/css/block_flow_state.cpp
    src/css/block_intrinsic_width.cpp
    src/css/block_float_context.cpp
    src/css/flex_layout.cpp
    src/css/flex_item_sizing.cpp
    src/css/grid_layout.cpp
    src/css/grid_structure.cpp
    src/css/grid_template.cpp
    src/css/grid_placement.cpp
    src/css/grid_positioned_area.cpp
    src/css/grid_item_sizing.cpp
    src/css/grid_item_contribution.cpp
    src/css/grid_track_sizing.cpp
    src/css/positioned_layout.cpp
    src/css/positioned_item_layout.cpp
    src/css/overflow_layout.cpp
    src/css/balanced_lines.cpp
    src/css_values.cpp
)
add_library(axisly::axisly ALIAS axisly)
if(AXISLY_ENABLE_GRID_METRICS)
    target_compile_definitions(axisly PRIVATE AXISLY_ENABLE_GRID_METRICS=1)
endif()
set_target_properties(axisly PROPERTIES
    EXPORT_NAME axisly
    CXX_EXTENSIONS OFF
)
target_compile_features(axisly PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(axisly PUBLIC Threads::Threads)
include(cmake/AxislyPgo.cmake)
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
    if(AXISLY_ENABLE_TSAN)
        target_compile_options(${target} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=thread)
    endif()
    if(AXISLY_ENABLE_COVERAGE)
        if(MSVC)
            target_compile_options(${target} PRIVATE /clang:-fprofile-instr-generate /clang:-fcoverage-mapping)
        else()
            target_compile_options(${target} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
            target_link_options(${target} PRIVATE -fprofile-instr-generate)
        endif()
    endif()
endfunction()
axisly_enable_warnings(axisly)
if(AXISLY_ENABLE_TSAN)
    target_link_options(axisly INTERFACE -fsanitize=thread)
endif()
if(AXISLY_ENABLE_COVERAGE)
    if(MSVC)
        if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR CMAKE_SYSTEM_PROCESSOR MATCHES "ARM|arm|aarch")
            message(FATAL_ERROR "Windows LLVM coverage currently requires x64")
        endif()
        execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -print-resource-dir
            OUTPUT_VARIABLE axisly_profile_resource OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
        set(axisly_profile_runtime "${axisly_profile_resource}/lib/windows/clang_rt.profile-x86_64.lib")
        if(NOT EXISTS "${axisly_profile_runtime}")
            message(FATAL_ERROR "Clang profile runtime is missing: ${axisly_profile_runtime}")
        endif()
        target_link_libraries(axisly INTERFACE "${axisly_profile_runtime}")
    else()
        target_link_options(axisly INTERFACE -fprofile-instr-generate)
    endif()
endif()
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
    add_executable(axisly_api_example examples/css_grid.cpp)
    target_link_libraries(axisly_api_example PRIVATE axisly::axisly)
    axisly_enable_warnings(axisly_api_example)
    if(AXISLY_BUILD_TESTS)
        add_test(NAME axisly.css.example COMMAND axisly_api_example)
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
