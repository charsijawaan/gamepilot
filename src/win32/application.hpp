#pragma once

#include <windows.h>

#include <expected>
#include <system_error>

namespace gamepilot::win32 {

[[nodiscard]] auto run_application(HINSTANCE instance, int show_command, bool smoke_test = false)
    -> std::expected<int, std::error_code>;

} // namespace gamepilot::win32
