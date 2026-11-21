// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "cores/types.h"

namespace mister::cores::snes {

class HeaderFields {
public:
    static constexpr HeaderLen kMapper{0x15};
    static constexpr HeaderLen kRomType{0x16};
    static constexpr HeaderLen kRomSize{0x17};
    static constexpr HeaderLen kRamSize{0x18};
    static constexpr HeaderLen kRegion{0x19};
    static constexpr HeaderLen kCompany{0x1a};
    static constexpr HeaderLen kComplement{0x1c};
    static constexpr HeaderLen kChecksum{0x1e};
    static constexpr HeaderLen kResetVector{0x3c};

    static constexpr std::uint8_t kFastRomBit = 0x10;
    static constexpr std::uint8_t kMapperClassMask = 0xef;

    static constexpr std::uint16_t kChecksumTotal = 0xffff;

    [[nodiscard]] static std::optional<HeaderFields> of(
        std::span<const std::uint8_t> window
    ) noexcept;

    [[nodiscard]] ResetVector reset_vector() const noexcept;

    [[nodiscard]] bool resets_into_rom() const noexcept;

    [[nodiscard]] bool checksum_valid() const noexcept;

    [[nodiscard]] std::uint8_t mapper_class() const noexcept;

    [[nodiscard]] bool extended_company() const noexcept;

    [[nodiscard]] bool rom_type_plausible() const noexcept;
    [[nodiscard]] bool rom_size_plausible() const noexcept;
    [[nodiscard]] bool ram_size_plausible() const noexcept;
    [[nodiscard]] bool region_plausible() const noexcept;

private:
    explicit constexpr HeaderFields(std::span<const std::uint8_t> window) noexcept
        : window_(window) {}

    std::span<const std::uint8_t> window_;
};

}
