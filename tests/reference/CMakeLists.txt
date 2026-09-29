if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "Reference tools must execute on the build host")
endif()
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
find_program(AXISLY_CARGO_EXECUTABLE cargo REQUIRED)
find_program(AXISLY_RUSTC_EXECUTABLE rustc REQUIRED)
execute_process(COMMAND "${AXISLY_RUSTC_EXECUTABLE}" -vV
    OUTPUT_VARIABLE rust_version OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
string(REGEX MATCH "host: ([A-Za-z0-9_-]+)" rust_host_match "${rust_version}")
if(NOT rust_host_match)
    message(FATAL_ERROR "Cannot determine the Rust host target")
endif()
set(rust_host "${CMAKE_MATCH_1}")
set(taffy_manifest "${CMAKE_CURRENT_SOURCE_DIR}/taffy/Cargo.toml")
set(taffy_output "${CMAKE_CURRENT_BINARY_DIR}/cargo/${rust_host}/release/axisly-taffy-reference${CMAKE_EXECUTABLE_SUFFIX}")
set(taffy_tree_output "${CMAKE_CURRENT_BINARY_DIR}/cargo/${rust_host}/release/axisly-taffy-tree-reference${CMAKE_EXECUTABLE_SUFFIX}")
set(taffy_calc_output "${CMAKE_CURRENT_BINARY_DIR}/cargo/${rust_host}/release/calc_reference${CMAKE_EXECUTABLE_SUFFIX}")
add_custom_command(OUTPUT "${taffy_output}" "${taffy_tree_output}" "${taffy_calc_output}"
    COMMAND "${AXISLY_CARGO_EXECUTABLE}" build --release --locked --bins
        --manifest-path "${taffy_manifest}" --target "${rust_host}"
        --target-dir "${CMAKE_CURRENT_BINARY_DIR}/cargo"
    DEPENDS "${taffy_manifest}" taffy/Cargo.lock taffy/src/main.rs taffy/src/advanced.rs
        taffy/src/bin/tree_reference.rs taffy/src/bin/tree/mod.rs taffy/src/bin/tree/style.rs
        taffy/src/bin/calc_reference.rs
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Building pinned Taffy reference tool (development only)" VERBATIM)
add_custom_target(axisly_taffy_reference ALL DEPENDS "${taffy_output}" "${taffy_tree_output}" "${taffy_calc_output}")

add_executable(axisly_reference_native native_runner.cpp)
target_link_libraries(axisly_reference_native PRIVATE axisly::axisly)
set_target_properties(axisly_reference_native PROPERTIES CXX_EXTENSIONS OFF)
axisly_enable_warnings(axisly_reference_native)

add_executable(axisly_css_reference_native css_native_runner.cpp)
target_link_libraries(axisly_css_reference_native PRIVATE axisly::axisly)
set_target_properties(axisly_css_reference_native PROPERTIES CXX_EXTENSIONS OFF)
axisly_enable_warnings(axisly_css_reference_native)

add_executable(axisly_flex_matrix_native flex_matrix_native.cpp)
target_link_libraries(axisly_flex_matrix_native PRIVATE axisly::axisly)
set_target_properties(axisly_flex_matrix_native PROPERTIES CXX_EXTENSIONS OFF)
axisly_enable_warnings(axisly_flex_matrix_native)

add_executable(axisly_flex_bounds_native flex_bounds_native.cpp)
target_link_libraries(axisly_flex_bounds_native PRIVATE axisly::axisly)
set_target_properties(axisly_flex_bounds_native PROPERTIES CXX_EXTENSIONS OFF)
axisly_enable_warnings(axisly_flex_bounds_native)

add_executable(axisly_flex_factors_native flex_factors_native.cpp)
target_link_libraries(axisly_flex_factors_native PRIVATE axisly::axisly)
set_target_properties(axisly_flex_factors_native PROPERTIES CXX_EXTENSIONS OFF)
axisly_enable_warnings(axisly_flex_factors_native)

add_executable(axisly_mixed_nested_native mixed_nested_native.cpp)
target_link_libraries(axisly_mixed_nested_native PRIVATE axisly::axisly)
set_target_properties(axisly_mixed_nested_native PROPERTIES CXX_EXTENSIONS OFF)
axisly_enable_warnings(axisly_mixed_nested_native)

add_executable(axisly_block_float_matrix_native block_float_matrix_native.cpp)
target_link_libraries(axisly_block_float_matrix_native PRIVATE axisly::axisly)
set_target_properties(axisly_block_float_matrix_native PROPERTIES CXX_EXTENSIONS OFF)
axisly_enable_warnings(axisly_block_float_matrix_native)

add_executable(axisly_grid_matrix_native grid_matrix_native.cpp)
target_link_libraries(axisly_grid_matrix_native PRIVATE axisly::axisly)
set_target_properties(axisly_grid_matrix_native PROPERTIES CXX_EXTENSIONS OFF)
axisly_enable_warnings(axisly_grid_matrix_native)

add_executable(axisly_grid_line_matrix_native grid_line_matrix_native.cpp)
target_link_libraries(axisly_grid_line_matrix_native PRIVATE axisly::axisly)
set_target_properties(axisly_grid_line_matrix_native PROPERTIES CXX_EXTENSIONS OFF)
axisly_enable_warnings(axisly_grid_line_matrix_native)

add_executable(axisly_grid_repeat_matrix_native grid_repeat_matrix_native.cpp)
target_link_libraries(axisly_grid_repeat_matrix_native PRIVATE axisly::axisly)
set_target_properties(axisly_grid_repeat_matrix_native PROPERTIES CXX_EXTENSIONS OFF)
axisly_enable_warnings(axisly_grid_repeat_matrix_native)

add_executable(axisly_grid_area_matrix_native grid_area_matrix_native.cpp)
target_link_libraries(axisly_grid_area_matrix_native PRIVATE axisly::axisly)
set_target_properties(axisly_grid_area_matrix_native PROPERTIES CXX_EXTENSIONS OFF)
axisly_enable_warnings(axisly_grid_area_matrix_native)

add_executable(axisly_grid_triple_span_native grid_triple_span_native.cpp)
target_link_libraries(axisly_grid_triple_span_native PRIVATE axisly::axisly)
set_target_properties(axisly_grid_triple_span_native PROPERTIES CXX_EXTENSIONS OFF)
axisly_enable_warnings(axisly_grid_triple_span_native)

add_executable(axisly_intrinsic_size_native intrinsic_size_native.cpp)
target_link_libraries(axisly_intrinsic_size_native PRIVATE axisly::axisly)
set_target_properties(axisly_intrinsic_size_native PROPERTIES CXX_EXTENSIONS OFF)
axisly_enable_warnings(axisly_intrinsic_size_native)

add_executable(axisly_ratio_matrix_native ratio_matrix_native.cpp)
target_link_libraries(axisly_ratio_matrix_native PRIVATE axisly::axisly)
set_target_properties(axisly_ratio_matrix_native PROPERTIES CXX_EXTENSIONS OFF)
axisly_enable_warnings(axisly_ratio_matrix_native)

add_executable(axisly_item_alignment_matrix_native item_alignment_matrix_native.cpp)
target_link_libraries(axisly_item_alignment_matrix_native PRIVATE axisly::axisly)
set_target_properties(axisly_item_alignment_matrix_native PROPERTIES CXX_EXTENSIONS OFF)
axisly_enable_warnings(axisly_item_alignment_matrix_native)

add_executable(axisly_baseline_matrix_native baseline_matrix_native.cpp)
target_link_libraries(axisly_baseline_matrix_native PRIVATE axisly::axisly)
set_target_properties(axisly_baseline_matrix_native PROPERTIES CXX_EXTENSIONS OFF)
axisly_enable_warnings(axisly_baseline_matrix_native)

add_executable(axisly_content_alignment_matrix_native content_alignment_matrix_native.cpp)
target_link_libraries(axisly_content_alignment_matrix_native PRIVATE axisly::axisly)
set_target_properties(axisly_content_alignment_matrix_native PROPERTIES CXX_EXTENSIONS OFF)
axisly_enable_warnings(axisly_content_alignment_matrix_native)

add_executable(axisly_position_matrix_native position_matrix_native.cpp)
target_link_libraries(axisly_position_matrix_native PRIVATE axisly::axisly)
set_target_properties(axisly_position_matrix_native PROPERTIES CXX_EXTENSIONS OFF)
axisly_enable_warnings(axisly_position_matrix_native)

add_test(NAME axisly.reference.compare COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/compare.py"
    --axisly "$<TARGET_FILE:axisly_reference_native>" --taffy "${taffy_output}"
    --failure-dir "${CMAKE_CURRENT_BINARY_DIR}/failures")
add_test(NAME axisly.reference.harness COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_reference.py"
    --axisly "$<TARGET_FILE:axisly_reference_native>" --taffy "${taffy_output}")
set_tests_properties(axisly.reference.compare axisly.reference.harness PROPERTIES LABELS reference TIMEOUT 120)

add_test(NAME axisly.reference.advanced COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/advanced.py"
    --axisly "$<TARGET_FILE:axisly_reference_native>" --taffy "${taffy_output}"
    --failure-dir "${CMAKE_CURRENT_BINARY_DIR}/advanced-failures")
add_test(NAME axisly.reference.advanced_harness COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_advanced.py"
    --axisly "$<TARGET_FILE:axisly_reference_native>" --taffy "${taffy_output}")
set_tests_properties(axisly.reference.advanced axisly.reference.advanced_harness PROPERTIES LABELS reference TIMEOUT 120)

add_test(NAME axisly.reference.tree COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_tree.py"
    --taffy "${taffy_tree_output}")
set_tests_properties(axisly.reference.tree PROPERTIES LABELS reference TIMEOUT 120)

add_test(NAME axisly.reference.css_tree COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_css_tree.py"
    --details
    --axisly "$<TARGET_FILE:axisly_css_reference_native>"
    --taffy "${taffy_tree_output}"
    --fixture "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tree.json")
set_tests_properties(axisly.reference.css_tree PROPERTIES LABELS reference TIMEOUT 120)

add_test(NAME axisly.reference.flex_matrix COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_flex_matrix.py"
    --axisly "$<TARGET_FILE:axisly_flex_matrix_native>"
    --taffy "${taffy_tree_output}")
set_tests_properties(axisly.reference.flex_matrix PROPERTIES LABELS reference TIMEOUT 120)

foreach(space IN ITEMS max-content min-content auto-definite)
    add_test(NAME axisly.reference.flex_${space}_matrix COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_flex_matrix.py"
        --scenario "${space}"
        --axisly "$<TARGET_FILE:axisly_flex_matrix_native>"
        --taffy "${taffy_tree_output}")
    set_tests_properties(axisly.reference.flex_${space}_matrix PROPERTIES LABELS reference TIMEOUT 120)
endforeach()

add_test(NAME axisly.reference.flex_bounds_matrix COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_flex_bounds.py"
    --axisly "$<TARGET_FILE:axisly_flex_bounds_native>"
    --taffy "${taffy_tree_output}")
set_tests_properties(axisly.reference.flex_bounds_matrix PROPERTIES LABELS reference TIMEOUT 120)

foreach(space IN ITEMS max-content min-content)
    add_test(NAME axisly.reference.flex_bounds_${space} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_flex_bounds.py"
        --query "${space}"
        --axisly "$<TARGET_FILE:axisly_flex_bounds_native>"
        --taffy "${taffy_tree_output}")
    set_tests_properties(axisly.reference.flex_bounds_${space} PROPERTIES LABELS reference TIMEOUT 120)
endforeach()

add_test(NAME axisly.reference.flex_factors_matrix COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_flex_factors.py"
    --axisly "$<TARGET_FILE:axisly_flex_factors_native>"
    --taffy "${taffy_tree_output}")
set_tests_properties(axisly.reference.flex_factors_matrix PROPERTIES LABELS reference TIMEOUT 120)

foreach(space IN ITEMS max-content min-content auto-definite)
    add_test(NAME axisly.reference.flex_factors_${space} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_flex_factors.py"
        --scenario "${space}"
        --axisly "$<TARGET_FILE:axisly_flex_factors_native>"
        --taffy "${taffy_tree_output}")
    set_tests_properties(axisly.reference.flex_factors_${space} PROPERTIES LABELS reference TIMEOUT 120)
endforeach()

foreach(space IN ITEMS definite max-content min-content auto-definite)
    add_test(NAME axisly.reference.mixed_nested_${space} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_mixed_nested.py"
        --scenario "${space}"
        --details
        --axisly "$<TARGET_FILE:axisly_mixed_nested_native>"
        --taffy "${taffy_tree_output}")
    set_tests_properties(axisly.reference.mixed_nested_${space} PROPERTIES LABELS reference TIMEOUT 120)
endforeach()

add_test(NAME axisly.reference.block_float_matrix COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_block_float_matrix.py"
    --axisly "$<TARGET_FILE:axisly_block_float_matrix_native>"
    --taffy "${taffy_tree_output}")
set_tests_properties(axisly.reference.block_float_matrix PROPERTIES LABELS reference TIMEOUT 120)

add_test(NAME axisly.reference.grid_matrix COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_grid_matrix.py"
    --axisly "$<TARGET_FILE:axisly_grid_matrix_native>"
    --taffy "${taffy_tree_output}"
    --details)
set_tests_properties(axisly.reference.grid_matrix PROPERTIES LABELS reference TIMEOUT 120)

add_test(NAME axisly.reference.grid_repeat_matrix COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_grid_repeat_matrix.py"
    --axisly "$<TARGET_FILE:axisly_grid_repeat_matrix_native>"
    --taffy "${taffy_tree_output}"
    --details)
set_tests_properties(axisly.reference.grid_repeat_matrix PROPERTIES LABELS reference TIMEOUT 120)

add_test(NAME axisly.reference.grid_area_matrix COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_grid_area_matrix.py"
    --axisly "$<TARGET_FILE:axisly_grid_area_matrix_native>"
    --taffy "${taffy_tree_output}"
    --details)
set_tests_properties(axisly.reference.grid_area_matrix PROPERTIES LABELS reference TIMEOUT 120)

foreach(start_line RANGE 0 11)
    add_test(NAME axisly.reference.grid_line_matrix.${start_line} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_grid_line_matrix.py"
        --axisly "$<TARGET_FILE:axisly_grid_line_matrix_native>"
        --taffy "${taffy_tree_output}"
        --start-line "${start_line}"
        --details)
    set_tests_properties(axisly.reference.grid_line_matrix.${start_line}
        PROPERTIES LABELS reference TIMEOUT 120)
endforeach()

add_test(NAME axisly.reference.grid_span_matrix COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_grid_span_matrix.py"
    --axisly "$<TARGET_FILE:axisly_grid_matrix_native>"
    --taffy "${taffy_tree_output}"
    --details)
set_tests_properties(axisly.reference.grid_span_matrix PROPERTIES LABELS reference TIMEOUT 120)

add_test(NAME axisly.reference.grid_triple_span COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_grid_triple_span.py"
    --axisly "$<TARGET_FILE:axisly_grid_triple_span_native>"
    --taffy "${taffy_tree_output}"
    --details)
set_tests_properties(axisly.reference.grid_triple_span PROPERTIES LABELS reference TIMEOUT 120)

add_test(NAME axisly.reference.intrinsic_size COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_intrinsic_size.py"
    --axisly "$<TARGET_FILE:axisly_intrinsic_size_native>"
    --taffy "${taffy_tree_output}"
    --details)
set_tests_properties(axisly.reference.intrinsic_size PROPERTIES LABELS reference TIMEOUT 120)

add_test(NAME axisly.reference.ratio_matrix COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_ratio_matrix.py"
    --axisly "$<TARGET_FILE:axisly_ratio_matrix_native>"
    --taffy "${taffy_tree_output}"
    --details)
set_tests_properties(axisly.reference.ratio_matrix PROPERTIES LABELS reference TIMEOUT 120)

add_test(NAME axisly.reference.item_alignment_matrix COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_item_alignment_matrix.py"
    --axisly "$<TARGET_FILE:axisly_item_alignment_matrix_native>"
    --taffy "${taffy_tree_output}")
set_tests_properties(axisly.reference.item_alignment_matrix PROPERTIES LABELS reference TIMEOUT 120)

add_test(NAME axisly.reference.baseline_matrix COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_baseline_matrix.py"
    --axisly "$<TARGET_FILE:axisly_baseline_matrix_native>"
    --taffy "${taffy_tree_output}")
set_tests_properties(axisly.reference.baseline_matrix PROPERTIES LABELS reference TIMEOUT 120)

add_test(NAME axisly.reference.content_alignment_matrix COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_content_alignment_matrix.py"
    --axisly "$<TARGET_FILE:axisly_content_alignment_matrix_native>"
    --taffy "${taffy_tree_output}")
set_tests_properties(axisly.reference.content_alignment_matrix PROPERTIES LABELS reference TIMEOUT 120)

add_test(NAME axisly.reference.position_matrix COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_position_matrix.py"
    --axisly "$<TARGET_FILE:axisly_position_matrix_native>"
    --taffy "${taffy_tree_output}")
set_tests_properties(axisly.reference.position_matrix PROPERTIES LABELS reference TIMEOUT 120)

add_test(NAME axisly.reference.calc COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_calc.py"
    --axisly "$<TARGET_FILE:axisly_css_reference_native>"
    --taffy "${taffy_calc_output}"
    --fixture "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/calc.json")
set_tests_properties(axisly.reference.calc PROPERTIES LABELS reference TIMEOUT 120)
