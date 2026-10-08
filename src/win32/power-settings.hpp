#pragma once

#include "core/gaming-mode.hpp"

#include <filesystem>

namespace gamepilot::win32 {

class windows_power_settings final : public power_settings {
  public:
    [[nodiscard]] auto active_plan() -> std::string override;
    [[nodiscard]] auto read(const std::string& plan) -> lid_settings override;
    void write(const lid_settings& settings) override;
};

class file_recovery_store final : public recovery_store {
  public:
    explicit file_recovery_store(std::filesystem::path path);
    [[nodiscard]] auto load() -> std::optional<lid_settings> override;
    void save(const lid_settings& settings) override;
    void clear() override;

  private:
    std::filesystem::path path_;
};

[[nodiscard]] auto recovery_path() -> std::filesystem::path;

} // namespace gamepilot::win32
