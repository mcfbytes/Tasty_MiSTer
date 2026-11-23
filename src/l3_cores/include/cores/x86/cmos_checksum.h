// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/x86/cmos_register.h"

namespace mister::cores::x86 {

struct CmosChecksum {
    std::uint16_t v = 0;

    [[nodiscard]] static constexpr CmosChecksum of(const X86CmosImage& image) noexcept {
        std::uint16_t sum = 0;
        for (CmosRegister reg = cmos::kSumFirst; reg <= cmos::kSumLast; reg += CmosRegisterCount{1})
            sum = static_cast<std::uint16_t>(sum + std::to_integer<std::uint16_t>(image[reg]));
        return CmosChecksum{sum};
    }

    constexpr void store(X86CmosImage& image) const noexcept {
        constexpr unsigned kByteBits = 8;
        image[cmos::kChecksumHigh] = std::byte{static_cast<std::uint8_t>(v >> kByteBits)};
        image[cmos::kChecksumLow] = std::byte{static_cast<std::uint8_t>(v)};
    }

    friend constexpr bool operator==(CmosChecksum, CmosChecksum) = default;
};

}
