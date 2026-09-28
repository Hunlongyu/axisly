if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "Reference tools must execute on the build host")
endif()
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
find_program(GRIDLY_CARGO_EXECUTABLE cargo REQUIRED)
find_program(GRIDLY_RUSTC_EXECUTABLE rustc REQUIRED)
execute_process(COMMAND "${GRIDLY_RUSTC_EXECUTABLE}" -vV
    OUTPUT_VARIABLE rust_version OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
string(REGEX MATCH "host: ([A-Za-z0-9_-]+)" rust_host_match "${rust_version}")
if(NOT rust_host_match)
    message(FATAL_ERROR "Cannot determine the Rust host target")
endif()
set(rust_host "${CMAKE_MATCH_1}")
set(taffy_manifest "${CMAKE_CURRENT_SOURCE_DIR}/taffy/Cargo.toml")
set(taffy_output "${CMAKE_CURRENT_BINARY_DIR}/cargo/${rust_host}/release/gridly-taffy-reference${CMAKE_EXECUTABLE_SUFFIX}")
add_custom_command(OUTPUT "${taffy_output}"
    COMMAND "${GRIDLY_CARGO_EXECUTABLE}" build --release --locked
        --manifest-path "${taffy_manifest}" --target "${rust_host}"
        --target-dir "${CMAKE_CURRENT_BINARY_DIR}/cargo"
    DEPENDS "${taffy_manifest}" taffy/Cargo.lock taffy/src/main.rs
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Building pinned Taffy reference tool (development only)" VERBATIM)
add_custom_target(gridly_taffy_reference ALL DEPENDS "${taffy_output}")

add_executable(gridly_reference_native native_runner.cpp)
target_link_libraries(gridly_reference_native PRIVATE gridly::gridly)
set_target_properties(gridly_reference_native PROPERTIES CXX_EXTENSIONS OFF)
gridly_enable_warnings(gridly_reference_native)

add_test(NAME gridly.reference.compare COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/compare.py"
    --gridly "$<TARGET_FILE:gridly_reference_native>" --taffy "${taffy_output}"
    --failure-dir "${CMAKE_CURRENT_BINARY_DIR}/failures")
add_test(NAME gridly.reference.harness COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_reference.py"
    --gridly "$<TARGET_FILE:gridly_reference_native>" --taffy "${taffy_output}")
set_tests_properties(gridly.reference.compare gridly.reference.harness PROPERTIES LABELS reference TIMEOUT 120)
