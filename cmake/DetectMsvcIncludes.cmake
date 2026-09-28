# Some Windows CMake/MSVC combinations decode UTF-8 compiler output using the
# active ANSI code page. Ninja then silently records zero header dependencies.
# Probe a local header and read the ORIGINAL bytes, without process-output
# decoding. This also works with localized installations lacking English MUI.
function(axisly_detect_msvc_includes)
    if(NOT PROJECT_IS_TOP_LEVEL OR NOT MSVC OR NOT CMAKE_GENERATOR MATCHES "Ninja")
        return()
    endif()
    set(probe_dir "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/axisly-includes")
    file(MAKE_DIRECTORY "${probe_dir}")
    file(WRITE "${probe_dir}/axisly-dependency-probe.h" "// Header dependency probe\n")
    file(WRITE "${probe_dir}/probe.cpp" "#include \"axisly-dependency-probe.h\"\n")
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" /nologo /utf-8 /showIncludes /c "${probe_dir}/probe.cpp" "/Fo${probe_dir}/probe.obj"
        OUTPUT_FILE "${probe_dir}/output.txt"
        ERROR_FILE "${probe_dir}/output.txt"
        RESULT_VARIABLE probe_result
    )
    file(READ "${probe_dir}/output.txt" probe_output)
    if(NOT probe_result EQUAL 0 OR NOT probe_output MATCHES "(^|[\r\n])([^\r\n]*: +)[^\r\n]*axisly-dependency-probe[.]h")
        message(FATAL_ERROR "Cannot detect MSVC header dependency prefix; see ${probe_dir}/output.txt")
    endif()
    set(raw_prefix "${CMAKE_MATCH_2}")
    set(prefix_stamp "${probe_dir}/verified-prefix.txt")
    set(previous_prefix "")
    if(EXISTS "${prefix_stamp}")
        file(READ "${prefix_stamp}" previous_prefix)
    endif()
    if(NOT previous_prefix STREQUAL raw_prefix)
        # Old objects may have valid-looking but empty dependency records.
        # Invalidate that generated cache once, then keep incremental builds.
        file(REMOVE "${CMAKE_BINARY_DIR}/.ninja_deps")
        file(WRITE "${prefix_stamp}" "${raw_prefix}")
    endif()
    set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${raw_prefix}" PARENT_SCOPE)
    set(CMAKE_C_CL_SHOWINCLUDES_PREFIX "${raw_prefix}" PARENT_SCOPE)
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${raw_prefix}" PARENT_SCOPE)
endfunction()
