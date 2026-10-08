#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace gamepilot {

struct lid_settings {
    std::string plan;
    std::uint32_t plugged_in{};
    std::uint32_t battery{};
    auto operator==(const lid_settings&) const -> bool = default;
};

class power_settings {
  public:
    virtual ~power_settings() = default;
    [[nodiscard]] virtual auto active_plan() -> std::string = 0;
    [[nodiscard]] virtual auto read(const std::string& plan) -> lid_settings = 0;
    virtual void write(const lid_settings& settings) = 0;
};

class recovery_store {
  public:
    virtual ~recovery_store() = default;
    [[nodiscard]] virtual auto load() -> std::optional<lid_settings> = 0;
    virtual void save(const lid_settings& settings) = 0;
    virtual void clear() = 0;
};

enum class mode_state { off, on, attention };
struct mode_status {
    mode_state state{mode_state::off};
    std::string detail;
    auto operator==(const mode_status&) const -> bool = default;
};

// Application-level mode controller. UI and Windows APIs stay outside this layer.
// New mode features belong here as coordinated operations with their own recovery state.
class gaming_mode final {
  public:
    gaming_mode(power_settings& power, recovery_store& store);
    void enable();
    void disable();
    [[nodiscard]] auto status() -> mode_status;
    [[nodiscard]] auto needs_restore() const noexcept -> bool { return original_.has_value(); }

  private:
    power_settings& power_;
    recovery_store& store_;
    std::optional<lid_settings> original_;
    bool enabled_{};
    std::string error_;
};

} // namespace gamepilot
