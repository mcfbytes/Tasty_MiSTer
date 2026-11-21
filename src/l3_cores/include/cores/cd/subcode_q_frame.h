// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/seat.h"
#include "svc/disc/track.h"

namespace mister::cores::cd {

class SubcodeQFrame {
    TASTY_SEAT_RESIDENT(RT);

public:
    [[nodiscard]] std::array<std::uint8_t, svc::disc::kSubcodeBytes> wire_block(
        std::span<const std::uint8_t> q
    ) const noexcept {
        constexpr std::size_t kBitsPerByte = 8;
        constexpr std::size_t kTopBit = kBitsPerByte - 1;

        std::array<std::uint8_t, svc::disc::kSubcodeBytes> out{};
        constexpr unsigned kQLane = svc::disc::SubcodeByte::kChannelBits
            - 1
            - svc::disc::SubcodeByte::kQChannel;
        for (std::size_t byte = 0; byte < q.size(); ++byte) {
            for (std::size_t bit = 0; bit < kBitsPerByte; ++bit) {
                const unsigned carried = (unsigned{q[byte]} >> (kTopBit - bit)) & 1u;
                out[byte * kBitsPerByte + bit] = static_cast<std::uint8_t>(carried << kQLane);
            }
        }
        return out;
    }
};

}
