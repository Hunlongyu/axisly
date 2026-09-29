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

add_executable(axisly_grid_matrix_native grid_matrix_native.cpp)
target_link_libraries(axisly_grid_matrix_native PRIVATE axisly::axisly)
set_target_properties(axisly_grid_matrix_native PROPERTIES CXX_EXTENSIONS OFF)
axisly_enable_warnings(axisly_grid_matrix_native)

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
    --axisly "$<TARGET_FILE:axisly_css_reference_native>"
    --taffy "${taffy_tree_output}"
    --fixture "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tree.json")
set_tests_properties(axisly.reference.css_tree PROPERTIES LABELS reference TIMEOUT 120)

add_test(NAME axisly.reference.flex_matrix COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_flex_matrix.py"
    --axisly "$<TARGET_FILE:axisly_flex_matrix_native>"
    --taffy "${taffy_tree_output}")
set_tests_properties(axisly.reference.flex_matrix PROPERTIES LABELS reference TIMEOUT 120)

add_test(NAME axisly.reference.grid_matrix COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_grid_matrix.py"
    --axisly "$<TARGET_FILE:axisly_grid_matrix_native>"
    --taffy "${taffy_tree_output}")
set_tests_properties(axisly.reference.grid_matrix PROPERTIES LABELS reference TIMEOUT 120)

add_test(NAME axisly.reference.calc COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_calc.py"
    --axisly "$<TARGET_FILE:axisly_css_reference_native>"
    --taffy "${taffy_calc_output}"
    --fixture "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/calc.json")
set_tests_properties(axisly.reference.calc PROPERTIES LABELS reference TIMEOUT 120)
