#pragma once

#include <iostream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gridly::examples {

class ConsoleSession {
public:
    ConsoleSession() noexcept {
#ifdef _WIN32
        DWORD processes[2]{};
        DWORD input_mode{};
        DWORD output_mode{};
        wait_on_exit_ = GetConsoleProcessList(processes, 2) == 1
            && processes[0] == GetCurrentProcessId()
            && GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &input_mode)
            && GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &output_mode);
#endif
    }

    ~ConsoleSession() {
        if (wait_on_exit_) {
            std::cout << "\nPress Enter to close..." << std::flush;
            std::cin.get();
        }
    }

    ConsoleSession(const ConsoleSession&) = delete;
    ConsoleSession& operator=(const ConsoleSession&) = delete;

private:
    bool wait_on_exit_{};
};

} // namespace gridly::examples
