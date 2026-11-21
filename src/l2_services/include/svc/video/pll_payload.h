// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <concepts>
#include <cstdint>

#include "hal/spi_halves.h"

namespace mister::svc::video {

struct PllPayload {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr hal::SpiHalves halves() const noexcept { return hal::SpiHalves::of(v); }

    template <std::integral Word>
    constexpr void store_into(Word& slot) const noexcept {
        slot = static_cast<Word>(v);
    }

    friend constexpr bool operator==(PllPayload, PllPayload) = default;
};

inline constexpr PllPayload kPllNPayload{0x10000};
inline constexpr PllPayload kPllChargePumpValue{2};
inline constexpr PllPayload kPllBandwidthValue{7};

}
