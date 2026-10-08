#include "core/gaming-mode.hpp"
#include "win32/power-settings.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

namespace {
using namespace gamepilot;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
template <typename F> void require_failure(F&& operation) {
    bool failed = false;
    try {
        operation();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, "Expected an operation to fail");
}

struct fake_store final : recovery_store {
    std::optional<lid_settings> saved;
    bool fail_save{}, fail_clear{};
    auto load() -> std::optional<lid_settings> override { return saved; }
    void save(const lid_settings& settings) override {
        if (fail_save) {
            throw std::runtime_error("disk full");
        }
        saved = settings;
    }
    void clear() override {
        if (fail_clear) {
            throw std::runtime_error("file busy");
        }
        saved.reset();
    }
};

struct fake_power final : power_settings {
    fake_store& store;
    std::string active{"plan-a"};
    std::map<std::string, lid_settings> plans{{"plan-a", {"plan-a", 1, 2}},
                                              {"plan-b", {"plan-b", 2, 3}}};
    int writes{};
    bool partial_failure{}, fail_restore{}, ignore_write{}, switch_during_write{};
    explicit fake_power(fake_store& recovery) : store{recovery} {}
    auto active_plan() -> std::string override { return active; }
    auto read(const std::string& plan) -> lid_settings override { return plans.at(plan); }
    void write(const lid_settings& settings) override {
        require(store.saved.has_value(), "Power mutation occurred before recovery was saved");
        ++writes;
        if (fail_restore) {
            throw std::runtime_error("access denied");
        }
        if (ignore_write) {
            return;
        }
        plans.at(settings.plan).plugged_in = settings.plugged_in;
        if (partial_failure) {
            partial_failure = false;
            throw std::runtime_error("battery write failed");
        }
        plans.at(settings.plan).battery = settings.battery;
        if (switch_during_write) {
            active = "plan-b";
        }
    }
};

struct fixture {
    fake_store store;
    fake_power power{store};
    gaming_mode mode{power, store};
};

void round_trip() {
    fixture f;
    const auto before = f.power.read("plan-a");
    f.mode.enable();
    require(f.mode.status().state == mode_state::on, "Mode should be on");
    require(f.power.read("plan-a") == lid_settings{"plan-a", 0, 0},
            "Both power sources must ignore lid close");
    f.mode.enable();
    require(f.power.writes == 1, "Repeated enable must preserve originals");
    f.mode.disable();
    require(f.power.read("plan-a") == before && !f.store.saved,
            "Restore exact values and clear recovery");
    require(f.mode.status().state == mode_state::off, "Mode should be off");
    f.mode.disable();
    require(f.power.writes == 2, "Repeated disable must not write");
}

void save_failure() {
    fixture f;
    f.store.fail_save = true;
    require_failure([&] { f.mode.enable(); });
    require(f.power.writes == 0 && !f.mode.needs_restore(),
            "Failed journal write must not mutate Windows");
    f.store.fail_save = false;
    f.mode.enable();
    require(f.mode.status().state == mode_state::on, "Enable must be retryable");
}

void partial_enable_rolls_back() {
    fixture f;
    f.power.partial_failure = true;
    require_failure([&] { f.mode.enable(); });
    require(f.power.read("plan-a") == lid_settings{"plan-a", 1, 2},
            "Partial failure must restore both sources");
    require(!f.store.saved && f.mode.status().state == mode_state::attention,
            "Explain failed enable after rollback");
}

void restore_failure_retries() {
    fixture f;
    f.mode.enable();
    f.power.fail_restore = true;
    require_failure([&] { f.mode.disable(); });
    require(f.store.saved && f.mode.needs_restore(),
            "Keep original settings when restoration fails");
    require(f.mode.status().state == mode_state::attention,
            "Never show off after failed restoration");
    require_failure([&] { f.mode.enable(); });
    f.power.fail_restore = false;
    f.mode.disable();
    require(!f.store.saved && f.mode.status().state == mode_state::off,
            "Restore retry must succeed");
}

void crash_recovery() {
    fixture f;
    f.mode.enable();
    gaming_mode restarted{f.power, f.store};
    require(restarted.status().state == mode_state::attention,
            "Saved recovery must not be reported as active mode");
    restarted.disable();
    require(f.power.read("plan-a") == lid_settings{"plan-a", 1, 2},
            "New controller must recover originals");
}

void failed_enable_and_rollback() {
    fixture f;
    f.power.fail_restore = true;
    require_failure([&] { f.mode.enable(); });
    require(f.store.saved && f.mode.needs_restore(),
            "Keep snapshot when enable and rollback both fail");
    require(f.mode.status().state == mode_state::attention, "Failed rollback requires attention");
    f.power.fail_restore = false;
    gaming_mode restarted{f.power, f.store};
    restarted.disable();
    require(!f.store.saved && f.power.read("plan-a") == lid_settings{"plan-a", 1, 2},
            "Recover failed transaction on next launch");
}

void external_changes() {
    fixture f;
    f.mode.enable();
    f.power.plans.at("plan-a").battery = 1;
    require(f.mode.status().state == mode_state::attention, "Detect external changes on battery");
    f.power.active = "plan-b";
    require(f.mode.status().state == mode_state::attention, "Detect power plan switch");
    f.mode.disable();
    require(f.power.active == "plan-b", "Restoration must not change active plan");
    require(f.power.read("plan-a") == lid_settings{"plan-a", 1, 2},
            "Restore original plan even after switch");
    require(f.power.read("plan-b") == lid_settings{"plan-b", 2, 3}, "Do not touch new plan");
    f.mode.enable();
    f.mode.disable();
    require(f.power.read("plan-b") == lid_settings{"plan-b", 2, 3},
            "New plan must have its own snapshot");
}

void verify_writes() {
    fixture f;
    f.power.ignore_write = true;
    require_failure([&] { f.mode.enable(); });
    require(f.mode.status().state == mode_state::attention,
            "Never report on if Windows ignores writes");
    f.power.ignore_write = false;
    f.mode.enable();
    f.power.ignore_write = true;
    require_failure([&] { f.mode.disable(); });
    require(f.store.saved.has_value(), "Read-back mismatch must preserve recovery");
}

void plan_changes_during_enable() {
    fixture f;
    f.power.switch_during_write = true;
    require_failure([&] { f.mode.enable(); });
    require(f.power.read("plan-a") == lid_settings{"plan-a", 1, 2},
            "Roll back if plan changes during enable");
    require(f.power.active == "plan-b" && !f.store.saved, "Do not switch back to old plan");
}

void journal_clear_failure() {
    fixture f;
    f.mode.enable();
    f.store.fail_clear = true;
    require_failure([&] { f.mode.disable(); });
    require(f.store.saved.has_value(), "Keep recovery when journal cleanup fails");
    f.store.fail_clear = false;
    f.mode.disable();
    require(!f.store.saved, "Retry cleanup successfully");
}

void already_do_nothing() {
    fixture f;
    f.power.plans.at("plan-a") = {"plan-a", 0, 0};
    f.mode.enable();
    f.mode.disable();
    require(f.power.read("plan-a") == lid_settings{"plan-a", 0, 0},
            "Restore do-nothing unchanged if it was original");
}

void real_journal() {
    const auto directory = std::filesystem::temp_directory_path() /
                           ("gamepilot-test-" + std::to_string(GetCurrentProcessId()) + "-" +
                            std::to_string(GetTickCount64()));
    const auto path = directory / "lid-recovery.txt";
    const auto cleanup = [&] {
        std::filesystem::remove(path);
        std::filesystem::remove(directory / "lid-recovery.txt.tmp");
        std::filesystem::remove(directory);
    };
    try {
        win32::file_recovery_store store{path};
        require(!store.load(), "Missing journal is normal");
        const lid_settings settings{"{381b4222-f694-41f0-9685-ff5bb260df2e}", 1, 2};
        store.save(settings);
        require(store.load() == settings, "Journal must round trip");
        store.save({settings.plan, 3, 0});
        require(store.load() == lid_settings{settings.plan, 3, 0},
                "Atomic journal replacement must work");
        const std::vector<std::string> corrupt{"",
                                               "gamepilot-lid-v2\n",
                                               "gamepilot-lid-v1\ninvalid 1 2\n",
                                               "gamepilot-lid-v1\n" + settings.plan + " 4 1\n",
                                               "gamepilot-lid-v1\n" + settings.plan +
                                                   " 1 2 extra\n",
                                               std::string(1025, 'x')};
        for (const auto& text : corrupt) {
            {
                std::ofstream output{path, std::ios::binary};
                output << text;
            }
            require_failure([&] { (void)store.load(); });
            require(std::filesystem::exists(path), "Corrupt recovery must be preserved");
        }
        store.clear();
        store.clear();
        require(!store.load(), "Cleared journal must be absent");
    } catch (...) {
        cleanup();
        throw;
    }
    cleanup();
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests{
        {"round trip and idempotence", round_trip},
        {"save failure", save_failure},
        {"partial enable rollback", partial_enable_rolls_back},
        {"restore failure and retry", restore_failure_retries},
        {"crash recovery", crash_recovery},
        {"failed enable and rollback", failed_enable_and_rollback},
        {"external changes", external_changes},
        {"read-back verification", verify_writes},
        {"plan switch during enable", plan_changes_during_enable},
        {"journal cleanup failure", journal_clear_failure},
        {"existing do-nothing setting", already_do_nothing},
        {"disk journal validation", real_journal}};
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
            return 1;
        }
    }
    return 0;
}
