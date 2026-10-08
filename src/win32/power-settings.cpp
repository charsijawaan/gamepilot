#include "win32/power-settings.hpp"

#include <objbase.h>
#include <powrprof.h>
#include <shlobj.h>
#include <windows.h>

#include <array>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace gamepilot::win32 {
namespace {

void check(DWORD result, const char* operation) {
    if (result != ERROR_SUCCESS) {
        throw std::system_error(static_cast<int>(result), std::system_category(), operation);
    }
}

auto parse_plan(const std::string& plan) -> GUID {
    if (plan.size() != 38 || plan.front() != '{' || plan.back() != '}') {
        throw std::runtime_error("Invalid power plan identifier in recovery record.");
    }
    const std::wstring wide{plan.begin(), plan.end()};
    GUID result{};
    if (FAILED(CLSIDFromString(wide.c_str(), &result)) || result == GUID_NULL) {
        throw std::runtime_error("Invalid power plan identifier in recovery record.");
    }
    return result;
}

void validate(const lid_settings& settings) {
    (void)parse_plan(settings.plan);
    if (settings.plugged_in > 3 || settings.battery > 3) {
        throw std::runtime_error("Invalid lid action in recovery record.");
    }
}

struct local_deleter {
    void operator()(GUID* value) const noexcept { LocalFree(value); }
};
struct task_deleter {
    void operator()(wchar_t* value) const noexcept { CoTaskMemFree(value); }
};
struct handle_deleter {
    void operator()(void* value) const noexcept { CloseHandle(value); }
};
using unique_handle = std::unique_ptr<void, handle_deleter>;

} // namespace

auto windows_power_settings::active_plan() -> std::string {
    GUID* raw{};
    check(PowerGetActiveScheme(nullptr, &raw), "Read active power plan");
    const std::unique_ptr<GUID, local_deleter> plan{raw};
    std::array<wchar_t, 39> text{};
    if (StringFromGUID2(*plan, text.data(), static_cast<int>(text.size())) == 0) {
        throw std::runtime_error("Could not format the active power plan identifier.");
    }
    const std::wstring_view wide{text.data()};
    return {wide.begin(), wide.end()};
}

auto windows_power_settings::read(const std::string& plan) -> lid_settings {
    const auto id = parse_plan(plan);
    DWORD ac{}, dc{};
    check(PowerReadACValueIndex(nullptr, &id, &GUID_SYSTEM_BUTTON_SUBGROUP, &GUID_LIDCLOSE_ACTION,
                                &ac),
          "Read plugged-in lid action");
    check(PowerReadDCValueIndex(nullptr, &id, &GUID_SYSTEM_BUTTON_SUBGROUP, &GUID_LIDCLOSE_ACTION,
                                &dc),
          "Read battery lid action");
    lid_settings result{plan, ac, dc};
    validate(result);
    return result;
}

void windows_power_settings::write(const lid_settings& settings) {
    validate(settings);
    const auto id = parse_plan(settings.plan);
    check(PowerWriteACValueIndex(nullptr, &id, &GUID_SYSTEM_BUTTON_SUBGROUP, &GUID_LIDCLOSE_ACTION,
                                 settings.plugged_in),
          "Set plugged-in lid action");
    check(PowerWriteDCValueIndex(nullptr, &id, &GUID_SYSTEM_BUTTON_SUBGROUP, &GUID_LIDCLOSE_ACTION,
                                 settings.battery),
          "Set battery lid action");
    // Restoring a previous plan must never intentionally switch the current power plan.
    if (parse_plan(active_plan()) == id) {
        check(PowerSetActiveScheme(nullptr, &id), "Apply lid settings");
    }
}

file_recovery_store::file_recovery_store(std::filesystem::path path) : path_{std::move(path)} {}

auto file_recovery_store::load() -> std::optional<lid_settings> {
    if (!std::filesystem::exists(path_)) {
        return std::nullopt;
    }
    if (std::filesystem::file_size(path_) > 1024) {
        throw std::runtime_error("Recovery record is too large. Preserve it for troubleshooting.");
    }
    std::ifstream input{path_, std::ios::binary};
    std::string header;
    lid_settings result;
    if (!std::getline(input, header) || header != "gamepilot-lid-v1" ||
        !(input >> result.plan >> result.plugged_in >> result.battery)) {
        throw std::runtime_error(
            "Could not read the recovery record. Preserve it for troubleshooting.");
    }
    input >> std::ws;
    if (!input.eof()) {
        throw std::runtime_error("Unexpected data in recovery record.");
    }
    validate(result);
    return result;
}

void file_recovery_store::save(const lid_settings& settings) {
    validate(settings);
    std::filesystem::create_directories(path_.parent_path());
    const auto temporary = std::filesystem::path{path_.wstring() + L".tmp"};
    const auto text = std::string{"gamepilot-lid-v1\n"} + settings.plan + "\n" +
                      std::to_string(settings.plugged_in) + " " + std::to_string(settings.battery) +
                      "\n";
    const auto raw = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    if (raw == INVALID_HANDLE_VALUE) {
        check(GetLastError(), "Create recovery record");
    }
    unique_handle file{raw};
    DWORD written{};
    if (!WriteFile(file.get(), text.data(), static_cast<DWORD>(text.size()), &written, nullptr)) {
        check(GetLastError(), "Write recovery record");
    }
    if (written != text.size()) {
        throw std::runtime_error("Incomplete recovery record write.");
    }
    if (!FlushFileBuffers(file.get())) {
        check(GetLastError(), "Flush recovery record");
    }
    file.reset();
    if (!MoveFileExW(temporary.c_str(), path_.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        check(GetLastError(), "Commit recovery record");
    }
}

void file_recovery_store::clear() {
    if (!DeleteFileW(path_.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
        check(GetLastError(), "Remove restored recovery record");
    }
}

auto recovery_path() -> std::filesystem::path {
    PWSTR raw{};
    const auto result = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw);
    const std::unique_ptr<wchar_t, task_deleter> directory{raw};
    if (FAILED(result)) {
        throw std::runtime_error("Could not find the local application data folder.");
    }
    return std::filesystem::path{directory.get()} / "gamepilot" / "lid-recovery.txt";
}

} // namespace gamepilot::win32
