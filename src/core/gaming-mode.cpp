#include "core/gaming-mode.hpp"

#include <stdexcept>
#include <utility>

namespace gamepilot {

gaming_mode::gaming_mode(power_settings& power, recovery_store& store)
    : power_{power}, store_{store}, original_{store.load()} {}

void gaming_mode::enable() {
    if (original_) {
        if (enabled_ && status().state == mode_state::on) {
            return;
        }
        throw std::runtime_error(
            "Restore the previous settings before enabling Gaming Mode again.");
    }
    error_.clear();
    try {
        auto original = power_.read(power_.active_plan());
        // Prepare in memory before saving; never modify Windows without a durable original.
        original_ = std::move(original);
        try {
            store_.save(*original_);
        } catch (...) {
            original_.reset();
            throw;
        }
        power_.write({original_->plan, 0, 0});
        const auto actual = power_.read(original_->plan);
        if (actual.plugged_in != 0 || actual.battery != 0 ||
            power_.active_plan() != original_->plan) {
            throw std::runtime_error(
                "Windows changed the power plan or did not accept the lid settings.");
        }
        enabled_ = true;
    } catch (const std::exception& failure) {
        auto detail = std::string{"Could not enable Gaming Mode. "} + failure.what();
        try {
            disable();
        } catch (const std::exception& rollback) {
            detail += std::string{" Recovery is still needed. "} + rollback.what();
        }
        error_ = std::move(detail);
        throw std::runtime_error(error_);
    }
}

void gaming_mode::disable() {
    enabled_ = false;
    try {
        if (original_) {
            power_.write(*original_);
            if (power_.read(original_->plan) != *original_) {
                throw std::runtime_error("Windows did not restore the original lid settings.");
            }
            // Keep the recovery record until both restoration and read-back succeed.
            store_.clear();
            original_.reset();
        }
        error_.clear();
    } catch (const std::exception& failure) {
        error_ = std::string{"Could not restore lid settings. Retry Restore settings. "} +
                 failure.what();
        throw std::runtime_error(error_);
    }
}

auto gaming_mode::status() -> mode_status {
    if (!error_.empty()) {
        return {mode_state::attention, error_};
    }
    if (!enabled_) {
        return original_ ? mode_status{mode_state::attention,
                                       "Saved lid settings need recovery. Choose Restore settings."}
                         : mode_status{mode_state::off, "Your normal lid settings are in effect."};
    }
    try {
        const auto plan = power_.active_plan();
        if (plan != original_->plan) {
            return {mode_state::attention, "The power plan changed. Restore settings, then turn "
                                           "Gaming Mode on for the new plan."};
        }
        const auto actual = power_.read(plan);
        if (actual.plugged_in != 0 || actual.battery != 0) {
            return {
                mode_state::attention,
                "The lid settings changed outside GamePilot. Restore settings, then enable again."};
        }
        return {mode_state::on,
                "Closing the lid keeps your laptop running on battery and plugged in."};
    } catch (const std::exception& failure) {
        return {mode_state::attention,
                std::string{"Cannot verify lid settings. "} + failure.what()};
    }
}

} // namespace gamepilot
