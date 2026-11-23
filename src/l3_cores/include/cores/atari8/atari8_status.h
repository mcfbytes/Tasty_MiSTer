// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/atari8/atari8_wire.h"
#include "cores/atari8/sio_types.h"
#include "hal/types.h"

namespace mister::cores::atari8 {

struct Atari8Status {
    hal::SpiWord v{};

    [[nodiscard]] constexpr bool soft_boot() const noexcept { return v.intersects(kSoftBoot); }

    [[nodiscard]] constexpr bool cold_boot() const noexcept { return v.intersects(kColdBoot); }

    [[nodiscard]] constexpr bool halted() const noexcept { return v.intersects(kHalt); }

    [[nodiscard]] constexpr bool mode_800() const noexcept { return v.intersects(kMode800); }

    [[nodiscard]] constexpr bool drives_read_only() const noexcept {
        return v.intersects(kReadOnly);
    }

    [[nodiscard]] constexpr bool pbi() const noexcept { return v.intersects(kPbi); }

    [[nodiscard]] constexpr bool atx_1050() const noexcept { return v.intersects(kAtx1050); }

    [[nodiscard]] constexpr SioSpeedChoice speed_choice() const noexcept {
        return SioSpeedChoice{v.masked(kSpeed).high_byte()};
    }

    [[nodiscard]] constexpr wire::Pause pause_mirror() const noexcept {
        return halted() ? wire::Pause::Halted : wire::Pause::Run;
    }

    friend constexpr bool operator==(Atari8Status, Atari8Status) = default;

    static constexpr hal::SpiWord kSoftBoot{0x0001};
    static constexpr hal::SpiWord kColdBoot{0x0002};
    static constexpr hal::SpiWord kHalt{0x0004};
    static constexpr hal::SpiWord kMode800{0x0008};
    static constexpr hal::SpiWord kReadOnly{0x0040};
    static constexpr hal::SpiWord kPbi{0x0080};
    static constexpr hal::SpiWord kSpeed{0x0700};
    static constexpr hal::SpiWord kAtx1050{0x8000};

    static constexpr hal::SpiWord kHotkeys{0x0007};

    static constexpr hal::SpiWord kDriveOptions{0x87C0};
};

}
