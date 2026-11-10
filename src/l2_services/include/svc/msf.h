// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "proto/types.h"
#include "svc/types.h"

namespace mister::svc {

inline constexpr std::uint32_t kFramesPerSecond = 75;
inline constexpr std::uint32_t kSecondsPerMinute = 60;
inline constexpr std::uint32_t kFramesPerMinute = kFramesPerSecond * kSecondsPerMinute;

struct Msf {
    std::uint8_t m = 0, s = 0, f = 0;

    [[nodiscard]] static constexpr Msf of_lba(std::int32_t lba) noexcept {
        const std::uint32_t v = lba > 0 ? static_cast<std::uint32_t>(lba) : 0u;
        return Msf{
            static_cast<std::uint8_t>((v / kFramesPerSecond) / kSecondsPerMinute),
            static_cast<std::uint8_t>((v / kFramesPerSecond) % kSecondsPerMinute),
            static_cast<std::uint8_t>(v % kFramesPerSecond)
        };
    }

    [[nodiscard]] static constexpr Msf of_lba_bcd_masked(std::uint32_t position_frames) noexcept {
        using Bcd = proto::PackedBcd;
        const std::uint32_t seconds = position_frames / kFramesPerSecond;
        constexpr std::size_t kFields = 3;
        const std::array<std::uint32_t, kFields> digits{
            seconds / kSecondsPerMinute,
            seconds % kSecondsPerMinute,
            position_frames % kFramesPerSecond
        };
        std::array<std::uint8_t, kFields> packed{};
        for (std::size_t i = 0; i < digits.size(); ++i) {
            packed[i] = static_cast<std::uint8_t>(
                (((digits[i] / Bcd::kRadix) % Bcd::kRadix) << Bcd::kNibbleBits)
                | (digits[i] % Bcd::kRadix)
            );
        }
        return Msf{packed.front(), packed[1], packed.back()};
    }

    [[nodiscard]] static constexpr Msf of_lba_stock(std::int32_t lba) noexcept {
        constexpr std::int32_t kFps = static_cast<std::int32_t>(kFramesPerSecond);
        constexpr std::int32_t kSpm = static_cast<std::int32_t>(kSecondsPerMinute);
        return Msf{
            static_cast<std::uint8_t>((lba / kFps) / kSpm),
            static_cast<std::uint8_t>((lba / kFps) % kSpm),
            static_cast<std::uint8_t>(lba % kFps)
        };
    }

    [[nodiscard]] static constexpr Msf of_wrapped(std::int32_t lba) noexcept {
        constexpr std::int32_t kPerMinute = static_cast<std::int32_t>(kFramesPerMinute);
        constexpr std::int32_t kFps = static_cast<std::int32_t>(kFramesPerSecond);
        const auto mm = static_cast<std::uint8_t>(lba / kPerMinute);
        const std::int32_t rem = lba - static_cast<std::int32_t>(mm) * kPerMinute;

        return Msf{
            proto::PackedBcd::unmasked_byte(mm),
            proto::PackedBcd::unmasked_byte(static_cast<std::uint8_t>(rem / kFps)),
            proto::PackedBcd::unmasked_byte(static_cast<std::uint8_t>(rem % kFps))
        };
    }

    [[nodiscard]] static constexpr Msf of_minute(TrackCount count) noexcept {
        return Msf{count.v, 0, 0};
    }

    [[nodiscard]] static constexpr proto::Lba lba_from_bcd(
        std::span<const std::byte, 3> bytes
    ) noexcept {
        const auto digit = [](std::byte b) {
            return proto::PackedBcd{std::to_integer<std::uint8_t>(b)}.value();
        };
        const auto minute = digit(bytes.front());
        const auto second = digit(bytes[1]);
        const auto frame = digit(bytes.back());
        return proto::Lba{minute * kFramesPerMinute + second * kFramesPerSecond + frame};
    }

    friend constexpr bool operator==(Msf, Msf) = default;
};

static_assert(sizeof(Msf) == 3, "the record is the three bytes a frame carries");
static_assert(Msf::of_lba(-4) == Msf{0, 0, 0}, "the clamped form cannot go negative");
static_assert(Msf::of_lba(150) == Msf{0, 2, 0}, "150 frames is 00:02:00");

static_assert(
    Msf::of_lba_stock(-4).f == 252 && Msf::of_lba_stock(-4).s == 0,
    "the stock form truncates the signed division into the byte"
);
static_assert(
    Msf::of_lba_bcd_masked(150) == Msf{0, 0x02, 0x00} && Msf::of_lba_bcd_masked(450'000).m == 0x00,
    "the masked form wraps the tens digit"
);
static_assert(Msf::of_wrapped(150) == Msf{0, 0x02, 0x00}, "the cdi form packs its bytes");
static_assert(Msf::of_wrapped(450'000).m == 0xA0, "100 minutes wraps the packed minute byte");
static_assert(Msf::of_minute(TrackCount{12}) == Msf{12, 0, 0}, "the count is the minute byte");

}
