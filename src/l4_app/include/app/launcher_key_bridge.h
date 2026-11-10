// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "app/raw_key_edge.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "os/key_injector.h"
#include "proto/types.h"
#include "svc/types.h"

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

    static constexpr std::size_t kPadSlots = svc::DeviceSlot::kCount;

    explicit LauncherKeyBridge(os::IKeyInjector& out) noexcept : out_(out) {}

    LauncherKeyBridge(const LauncherKeyBridge&) = delete;
    LauncherKeyBridge& operator=(const LauncherKeyBridge&) = delete;

    [[nodiscard]] bool set_active(bool on) noexcept;

    [[nodiscard]] bool active() const noexcept { return active_; }

    void key(svc::EvdevCode code, bool down) noexcept;

    void pad(svc::DeviceSlot slot, svc::JoyMask level) noexcept;

    void reseat_pad(svc::DeviceSlot slot, svc::JoyMask level) noexcept;

    [[nodiscard]] const Counters& counters() const noexcept { return n_; }

private:
    void sync_(svc::EvdevCode code) noexcept;
    [[nodiscard]] svc::JoyMask pad_any_() const noexcept;
    void release_all_() noexcept;

    void inject_(svc::EvdevCode code, bool down) noexcept;

    os::IKeyInjector& out_;
    bool active_ = false;
    bool opened_ = false;
    KeyDownSet sent_{};
    KeyDownSet held_{};

    infra::IndexedArray<svc::JoyMask, kPadSlots, svc::DeviceSlot> pad_level_{};
    svc::JoyMask pad_or_{};
    Counters n_{};
};

}
