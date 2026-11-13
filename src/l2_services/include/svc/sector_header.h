// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include "svc/msf.h"

namespace mister::svc {

struct SectorHeader {
    enum class Mode : std::uint8_t { Mode1 = 1, Mode2 = 2 };
    static constexpr std::size_t kBytes = 16;

    static constexpr std::array<std::byte, 12> kSync{
        std::byte{0x00},
        std::byte{0xFF},
        std::byte{0xFF},
        std::byte{0xFF},
        std::byte{0xFF},
        std::byte{0xFF},
        std::byte{0xFF},
        std::byte{0xFF},
        std::byte{0xFF},
        std::byte{0xFF},
        std::byte{0xFF},
        std::byte{0x00}
    };
    Msf position{};
    Mode mode = Mode::Mode1;

    [[nodiscard]] static constexpr SectorHeader mode1_at(Msf packed_bcd) noexcept {
        return SectorHeader{packed_bcd, Mode::Mode1};
    }

    constexpr void write(std::span<std::byte, kBytes> out) const noexcept;

    [[nodiscard]] static constexpr std::optional<SectorHeader> read(
        std::span<const std::byte, kBytes> in
    ) noexcept;

    [[nodiscard]] constexpr proto::Lba lba() const noexcept { return position.lba_of_bcd(); }

    friend constexpr bool operator==(SectorHeader, SectorHeader) = default;

private:
    [[nodiscard]] static constexpr std::byte byte_of(Mode m) noexcept {
        return std::byte{std::to_underlying(m)};
    }
};

constexpr void SectorHeader::write(std::span<std::byte, kBytes> out) const noexcept {
    std::ranges::copy(kSync, out.begin());
    const std::span<std::byte, 3> at = out.subspan<SectorHeader::kSync.size(), 3>();
    at.front() = std::byte{position.m};
    at[1] = std::byte{position.s};
    at.back() = std::byte{position.f};
    out.back() = byte_of(mode);
}

constexpr std::optional<SectorHeader> SectorHeader::read(
    std::span<const std::byte, kBytes> in
) noexcept {
    if (!std::ranges::equal(in.first<SectorHeader::kSync.size()>(), kSync))
        return std::nullopt;
    const std::span<const std::byte, 3> at = in.subspan<SectorHeader::kSync.size(), 3>();
    const Msf pos{
        std::to_integer<std::uint8_t>(at.front()),
        std::to_integer<std::uint8_t>(at[1]),
        std::to_integer<std::uint8_t>(at.back())
    };
    for (const Mode m : {Mode::Mode1, Mode::Mode2}) {
        if (in.back() == byte_of(m))
            return SectorHeader{pos, m};
    }
    return std::nullopt;
}

}
