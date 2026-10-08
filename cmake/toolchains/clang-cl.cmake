# clang-cl uses LLVM code generation with the MSVC ABI, SDK and runtime.
# Run configuration/build from a Visual Studio developer environment.
if(NOT CMAKE_HOST_WIN32)
    message(FATAL_ERROR "The clang-cl toolchain requires a Windows host")
endif()

find_program(CMAKE_CXX_COMPILER NAMES clang-cl
    HINTS "$ENV{ProgramFiles}/LLVM/bin" REQUIRED)
find_program(CMAKE_C_COMPILER NAMES clang-cl
    HINTS "$ENV{ProgramFiles}/LLVM/bin" REQUIRED)
find_program(CMAKE_LINKER NAMES lld-link
    HINTS "$ENV{ProgramFiles}/LLVM/bin" REQUIRED)
