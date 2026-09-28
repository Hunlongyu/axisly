if(NOT WIN32)
    message(FATAL_ERROR "The pinned GeeyoouUI host requires Windows")
endif()
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
include(FetchContent)
set(GEEYOOU_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GEEYOOU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BLEND2D_NO_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(geeyoou
    GIT_REPOSITORY https://github.com/sj7suren/GeeyoouUI.git
    GIT_TAG 4cadaf6479b663ce6633e59bfbac28332de495fc
    GIT_SHALLOW OFF)
FetchContent_MakeAvailable(geeyoou)

set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/prepare.py")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/prepare.py"
    "${geeyoou_SOURCE_DIR}" "${generated}"
    COMMAND_ERROR_IS_FATAL ANY)
# One explicit, version-checked hook: content dirtiness reaches the adapter's
# revision cache. Never modify the fetched source or a caller's source override.
get_target_property(host_sources geeyoou SOURCES)
list(REMOVE_ITEM host_sources src/widget/Widget.cpp src/widget/Layout.cpp)
set_property(TARGET geeyoou PROPERTY SOURCES "${host_sources};${generated}/Widget.cpp;${generated}/Layout.cpp")
if(AXISLY_ENABLE_SANITIZERS)
    axisly_enable_warnings(geeyoou)
endif()

set(page_names Shell ShowcaseWindow PlantIcons PageWindow PageTheme PageDialogs
    PageIcons PageHmi PageWidgets PageInputs PageSelects PageOps
    PageTables PageTablesTree PageScene3D)
set(page_sources AxislyLayout.cpp PageOverview.cpp PageLayout.cpp)
foreach(page IN LISTS page_names)
    list(APPEND page_sources "${generated}/showcase/${page}.cpp")
endforeach()
foreach(lang I18n lang_zh_CN lang_en_US)
    list(APPEND page_sources "${generated}/showcase/i18n/${lang}.cpp")
endforeach()
add_library(axisly_geeyoou_pages STATIC ${page_sources})
target_link_libraries(axisly_geeyoou_pages PUBLIC axisly::axisly geeyoou::geeyoou)
target_include_directories(axisly_geeyoou_pages PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}" "${generated}/showcase")
axisly_enable_warnings(axisly_geeyoou_pages)

add_executable(axisly_geeyoou_showcase WIN32 "${generated}/showcase/main.cpp" Smoke.cpp)
target_link_libraries(axisly_geeyoou_showcase PRIVATE axisly_geeyoou_pages)
target_link_options(axisly_geeyoou_showcase PRIVATE /ENTRY:mainCRTStartup)
axisly_enable_warnings(axisly_geeyoou_showcase)
set_target_properties(axisly_geeyoou_showcase PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(AXISLY_BUILD_TESTS)
    add_executable(axisly_geeyoou_tests tests.cpp)
    target_link_libraries(axisly_geeyoou_tests PRIVATE axisly_geeyoou_pages)
    axisly_enable_warnings(axisly_geeyoou_tests)
    add_test(NAME axisly.geeyoou.integration COMMAND axisly_geeyoou_tests)
    set_tests_properties(axisly.geeyoou.integration PROPERTIES TIMEOUT 180)
    add_test(NAME axisly.geeyoou.showcase COMMAND axisly_geeyoou_showcase
        --smoke "${CMAKE_CURRENT_BINARY_DIR}/acceptance")
    set_tests_properties(axisly.geeyoou.showcase PROPERTIES TIMEOUT 180)
endif()
