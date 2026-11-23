// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "hal/reg_field.h"
#include "infra/endian.h"

namespace mister::cores::x86 {

struct DosOpenMode {
    using Field = hal::RegField<std::uint8_t>;
    using WordField = hal::RegField<std::uint16_t>;

    static constexpr Field kAccess{0, 0x03};
    static constexpr WordField kWire{0, 0x7F};

    enum class Access : std::uint8_t { Read = 0, Write = 1, ReadWrite = 2, Neither = 3 };

    std::uint8_t v = 0;

    [[nodiscard]] static constexpr DosOpenMode of_bits(std::byte bits) noexcept {
        return DosOpenMode{
            static_cast<std::uint8_t>(kAccess.get(std::to_integer<std::uint8_t>(bits)))
        };
    }

    [[nodiscard]] static constexpr DosOpenMode decode(std::span<const std::byte> bytes) noexcept {
        if (bytes.size() < sizeof(std::uint16_t))
            return {};
        return DosOpenMode{static_cast<std::uint8_t>(
            kWire.get(infra::load_le<std::uint16_t>(bytes.first<sizeof(std::uint16_t)>()))
        )};
    }

    [[nodiscard]] constexpr Access access() const noexcept {
        return Access{static_cast<std::uint8_t>(kAccess.get(v))};
    }

    constexpr void store(std::span<std::byte, 1> out) const noexcept { out[0] = std::byte{v}; }

    friend constexpr bool operator==(DosOpenMode, DosOpenMode) = default;
};

inline constexpr DosOpenMode kDosOpenReadWrite{2};

[[nodiscard]] constexpr bool dos_access_reads(DosOpenMode::Access access) noexcept {
    return access != DosOpenMode::Access::Write && access != DosOpenMode::Access::Neither;
}

[[nodiscard]] constexpr bool dos_access_writes(DosOpenMode::Access access) noexcept {
    return access != DosOpenMode::Access::Read && access != DosOpenMode::Access::Neither;
}

}
