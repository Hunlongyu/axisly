# Every pinned upstream input is part of the development reference gate.
include(FetchContent)
FetchContent_Declare(axisly_taffy_upstream
    URL "https://codeload.github.com/DioxusLabs/taffy/zip/77f385683c1d698c91a23a259f87fdddf26925fb"
    URL_HASH SHA256=baf36d0a3f896d901b81c241b332e2fc59f2f1b6ab05c574b8323b29e35d0baa
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
FetchContent_MakeAvailable(axisly_taffy_upstream)

set(axisly_upstream_generated "${CMAKE_CURRENT_BINARY_DIR}/upstream-generated")
file(MAKE_DIRECTORY "${axisly_upstream_generated}")
set(axisly_upstream_cases "${axisly_upstream_generated}/cases.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_fixture.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_expectations.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/css_case_codegen.py")
execute_process(COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_fixture.py"
    --source "${axisly_taffy_upstream_SOURCE_DIR}" --output "${axisly_upstream_cases}"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/css_case_codegen.py"
    --cases "${axisly_upstream_cases}" --output "${axisly_upstream_generated}"
    COMMAND_ERROR_IS_FATAL ANY)
include("${axisly_upstream_generated}/sources.cmake")
add_executable(axisly_upstream_native upstream_fixture_runner.cpp
    ${axisly_upstream_generated_sources})
target_link_libraries(axisly_upstream_native PRIVATE axisly::axisly)
target_include_directories(axisly_upstream_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(axisly_upstream_native PROPERTIES CXX_EXTENSIONS OFF)
axisly_enable_warnings(axisly_upstream_native)
if(MSVC)
    # Some upstream explicit-track fixtures exceed COFF's small section limit.
    target_compile_options(axisly_upstream_native PRIVATE /bigobj)
endif()

add_custom_target(axisly_upstream_audit
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_upstream_corpus.py"
        --cases "${axisly_upstream_cases}"
        --axisly "$<TARGET_FILE:axisly_upstream_native>"
        --taffy "${taffy_tree_output}"
        --output "${CMAKE_CURRENT_BINARY_DIR}/upstream-audit.json"
    DEPENDS axisly_upstream_native axisly_taffy_reference
    USES_TERMINAL VERBATIM)

add_test(NAME axisly.reference.upstream_adapter COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_upstream_adapter.py")
set_tests_properties(axisly.reference.upstream_adapter PROPERTIES LABELS reference TIMEOUT 30)

add_test(NAME axisly.reference.upstream_expectations COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_upstream_expectations.py")
set_tests_properties(axisly.reference.upstream_expectations PROPERTIES LABELS reference TIMEOUT 30)

add_test(NAME axisly.reference.upstream COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_upstream_corpus.py"
    --cases "${axisly_upstream_cases}"
    --axisly "$<TARGET_FILE:axisly_upstream_native>"
    --taffy "${taffy_tree_output}"
    --output "${CMAKE_CURRENT_BINARY_DIR}/upstream-audit.json")
set_tests_properties(axisly.reference.upstream PROPERTIES LABELS reference TIMEOUT 240)
