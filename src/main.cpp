#include "win32/application.hpp"

#include <exception>
#include <string>
#include <string_view>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR arguments, int show_command) {
    // Debuggers may append whitespace to the raw Win32 command line.
    auto options = std::wstring_view{arguments};
    const auto end = options.find_last_not_of(L" \t\r\n");
    options = end == std::wstring_view::npos ? std::wstring_view{} : options.substr(0, end + 1);
    const bool smoke_test = options == L"--smoke-test";
    try {
        const auto result = gamepilot::win32::run_application(instance, show_command, smoke_test);
        if (result) {
            return *result;
        }

        if (smoke_test) {
            return 1;
        }
        const auto message =
            L"Could not start GamePilot. Windows error: " + std::to_wstring(result.error().value());
        MessageBoxW(nullptr, message.c_str(), L"GamePilot", MB_OK | MB_ICONERROR);
    } catch (const std::exception& error) {
        if (!smoke_test) {
            const auto detail = std::string_view{error.what()};
            const auto length = MultiByteToWideChar(CP_UTF8, 0, detail.data(),
                                                    static_cast<int>(detail.size()), nullptr, 0);
            std::wstring message(static_cast<std::size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, detail.data(), static_cast<int>(detail.size()),
                                message.data(), length);
            message =
                L"Could not start GamePilot.\n\n" + message +
                L"\n\nAny saved recovery remains in %LOCALAPPDATA%\\gamepilot\\lid-recovery.txt.";
            MessageBoxW(nullptr, message.c_str(), L"GamePilot", MB_OK | MB_ICONERROR);
        }
    }
    return 1;
}
