// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>

#include "cores/atari8/pbi_drive_mode.h"
#include "cores/atari8/sio_types.h"
#include "hal/types.h"
#include "infra/indexed_array.h"

namespace mister::cores::atari8 {

struct Atari8Status2 {
    hal::SpiWord v{};

    struct BootDrive {
        std::uint8_t v = 0;
        static const BootDrive kApt;

        [[nodiscard]] constexpr bool chosen() const noexcept { return v != 0; }

        [[nodiscard]] constexpr bool apt() const noexcept { return *this == kApt; }

        [[nodiscard]] constexpr std::byte wire_byte() const noexcept { return std::byte{v}; }

        [[nodiscard]] constexpr std::byte drive_byte() const noexcept {
            return std::byte{static_cast<std::uint8_t>(v - 1)};
        }

        friend constexpr bool operator==(BootDrive, BootDrive) = default;
    };

    static constexpr SioDriveCount kModedDrives{4};

    static constexpr std::array<hal::RegField<std::uint8_t>, kModedDrives.v> kModeFields = {
        {{0, 0x3}, {2, 0x3}, {4, 0x3}, {6, 0x3}}
    };
    static constexpr hal::SpiWord kBootDrive{0x0700};
    static constexpr hal::SpiWord kSplash{0x0800};
    static constexpr hal::SpiWord kTapeFull{0x8000};
    static constexpr hal::SpiWord kTapeEmpty{0x4000};
    static constexpr hal::SpiWord kTapeActive{0x2000};
    static constexpr hal::SpiWord kTapeSlow{0x1000};

    [[nodiscard]] constexpr std::optional<PbiDriveMode> mode_of(SioUnit unit) const noexcept {
        if (!infra::affine::in_bounds(kModedDrives, unit))
            return std::nullopt;
        const hal::RegField<std::uint8_t> field = infra::at_index(kModeFields, unit);
        const std::uint8_t bits = field.get(std::to_integer<std::uint8_t>(v.low_lane()));
        for (const PbiDriveMode mode : {PbiDriveMode::Pbi, PbiDriveMode::Sio})
            if (bits == std::to_integer<std::uint8_t>(config_byte(mode)))
                return mode;
        return PbiDriveMode::Off;
    }

    [[nodiscard]] constexpr std::array<std::byte, kModedDrives.v> config_bytes() const noexcept {
        std::array<std::byte, kModedDrives.v> out{};
        for (SioUnit u{}; infra::affine::in_bounds(kModedDrives, u); u += SioDriveCount{1})
            infra::at_index(out, u) = config_byte(*mode_of(u));
        return out;
    }

    [[nodiscard]] constexpr BootDrive boot_drive() const noexcept {
        return BootDrive{v.masked(kBootDrive).high_byte()};
    }

    [[nodiscard]] constexpr bool splash() const noexcept { return v.intersects(kSplash); }

    [[nodiscard]] constexpr bool tape_full() const noexcept { return v.intersects(kTapeFull); }

    [[nodiscard]] constexpr bool tape_empty() const noexcept { return v.intersects(kTapeEmpty); }

    [[nodiscard]] constexpr bool tape_active() const noexcept { return v.intersects(kTapeActive); }

    [[nodiscard]] constexpr bool tape_slow() const noexcept { return v.intersects(kTapeSlow); }

    friend constexpr bool operator==(Atari8Status2, Atari8Status2) = default;
};

inline constexpr Atari8Status2::BootDrive Atari8Status2::BootDrive::kApt{1};

}
