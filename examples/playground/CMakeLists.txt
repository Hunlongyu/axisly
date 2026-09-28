if(GRIDLY_FETCH_SDL3)
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz
        URL_HASH SHA256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(SDL3)
    set(BUILD_SHARED_LIBS OFF)
    set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
    set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
    set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
    set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
    set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3_ttf
        URL https://github.com/libsdl-org/SDL_ttf/releases/download/release-3.2.2/SDL3_ttf-3.2.2.tar.gz
        URL_HASH SHA256=63547d58d0185c833213885b635a2c0548201cc8f301e6587c0be1a67e1e045d
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR gridly-populate-only
    )
    FetchContent_MakeAvailable(SDL3_ttf)
    # Release archives omit submodules. Fetch exactly the FreeType commit pinned
    # by SDL_ttf 3.2.2, before configuring its vendored backend.
    FetchContent_Declare(gridly_freetype
        URL https://github.com/libsdl-org/freetype/archive/9973564cfa63763a3e4ac67c09147899539b1e07.tar.gz
        URL_HASH SHA256=026a05a49d114a1235d2926f4c03a9330e4b1a6efe7c217ec9607904c32907d4
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_DIR "${sdl3_ttf_SOURCE_DIR}/external/freetype"
        SOURCE_SUBDIR gridly-populate-only
    )
    FetchContent_MakeAvailable(gridly_freetype)
    add_subdirectory("${sdl3_ttf_SOURCE_DIR}" "${sdl3_ttf_BINARY_DIR}")
    if(MSVC)
        target_compile_options(freetype PRIVATE /utf-8)
    endif()
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/playground-licenses")
    configure_file("${sdl3_SOURCE_DIR}/LICENSE.txt" "${CMAKE_BINARY_DIR}/playground-licenses/SDL.txt" COPYONLY)
    configure_file("${sdl3_ttf_SOURCE_DIR}/LICENSE.txt" "${CMAKE_BINARY_DIR}/playground-licenses/SDL_ttf.txt" COPYONLY)
    configure_file("${gridly_freetype_SOURCE_DIR}/docs/FTL.TXT" "${CMAKE_BINARY_DIR}/playground-licenses/FreeType.txt" COPYONLY)
else()
    find_package(SDL3 3.4 CONFIG REQUIRED)
    find_package(SDL3_ttf 3.2 CONFIG REQUIRED)
endif()

add_library(gridly_playground_scenes STATIC scenes.cpp)
target_link_libraries(gridly_playground_scenes PUBLIC gridly::gridly)
target_include_directories(gridly_playground_scenes PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(gridly_playground_scenes PROPERTIES CXX_EXTENSIONS OFF)
gridly_enable_warnings(gridly_playground_scenes)

add_executable(gridly_playground WIN32 main.cpp text_painter.cpp)
target_link_libraries(gridly_playground PRIVATE gridly_playground_scenes SDL3::SDL3 SDL3_ttf::SDL3_ttf)
set_target_properties(gridly_playground PROPERTIES
    CXX_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
gridly_enable_warnings(gridly_playground)
# Installed SDL packages may be shared; keep the executable directly runnable.
if(WIN32 AND TARGET SDL3::SDL3-shared)
    add_custom_command(TARGET gridly_playground POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3-shared> $<TARGET_FILE_DIR:gridly_playground>
        VERBATIM
    )
endif()
if(WIN32 AND TARGET SDL3_ttf::SDL3_ttf-shared)
    add_custom_command(TARGET gridly_playground POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:gridly_playground> $<TARGET_FILE_DIR:gridly_playground>
        COMMAND_EXPAND_LISTS VERBATIM
    )
endif()

if(GRIDLY_BUILD_TESTS)
    add_executable(gridly_playground_tests ../../tests/playground_tests.cpp)
    target_link_libraries(gridly_playground_tests PRIVATE gridly_playground_scenes)
    gridly_enable_warnings(gridly_playground_tests)
    add_test(NAME gridly.playground.scenes COMMAND gridly_playground_tests)
    add_test(NAME gridly.playground.runtime COMMAND gridly_playground
        --smoke-test "${CMAKE_CURRENT_BINARY_DIR}/captures")
    set_tests_properties(gridly.playground.runtime PROPERTIES
        TIMEOUT 45 ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software"
    )
    set_tests_properties(gridly.playground.scenes gridly.playground.runtime PROPERTIES LABELS playground)
endif()
