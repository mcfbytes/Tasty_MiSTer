// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "infra/seat.h"
#include "os/key_injector.h"

namespace mister::app {

class LauncherKeyBridge {
    TASTY_SEAT_RESIDENT(Input);

public:
    struct Counters {
        std::uint32_t keys = 0;
        std::uint32_t pad_keys = 0;
        std::uint32_t releases = 0;
        std::uint32_t open_failures = 0;
    };

    static constexpr std::size_t kPadSlots = 32;

    explicit LauncherKeyBridge(os::IKeyInjector& out) noexcept : out_(out) {}
    LauncherKeyBridge(const LauncherKeyBridge&) = delete;
    LauncherKeyBridge& operator=(const LauncherKeyBridge&) = delete;

    [[nodiscard]] bool set_active(bool on) noexcept;
    [[nodiscard]] bool active() const noexcept { return active_; }
    void key(std::uint16_t code, bool down) noexcept;

    void pad(std::size_t slot, std::uint32_t level) noexcept;

    void reseat_pad(std::size_t slot, std::uint32_t level) noexcept;

    [[nodiscard]] const Counters& counters() const noexcept { return n_; }

private:
    void sync_(std::uint16_t code) noexcept;
    [[nodiscard]] std::uint32_t pad_any_() const noexcept;
    void release_all_() noexcept;

    os::IKeyInjector& out_;
    bool active_ = false;
    bool opened_ = false;
    std::uint64_t down_[4] = {};
    std::uint64_t kbd_down_[4] = {};
    std::array<std::uint32_t, kPadSlots> pad_level_{};
    std::uint32_t pad_or_ = 0;
    Counters n_{};
};

}  // namespace mister::app
