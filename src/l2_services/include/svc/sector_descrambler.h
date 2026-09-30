// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "infra/seat.h"
#include "svc/frame_shaper.h"

namespace mister::svc {

consteval std::array<std::uint8_t, kCdDataSize - 12> make_sector_scramble() {
    std::array<std::uint8_t, kCdDataSize - 12> t{};
    unsigned reg = 1;
    for (auto& byte : t) {
        unsigned out = 0;
        for (unsigned bit = 0; bit < 8; ++bit) {
            out |= (reg & 1u) << bit;
            const unsigned fb = (reg ^ (reg >> 1)) & 1u;
            reg = (reg >> 1) | (fb << 14);
        }
        byte = static_cast<std::uint8_t>(out);
    }
    return t;
}
inline constexpr auto kSectorScramble = make_sector_scramble();

class SectorDescrambler final : public IFrameShaper {
    TASTY_SEAT_RESIDENT(Io);

public:
    void shape(proto::Lba lba, std::span<std::byte, kCdFrameSize> frame) const noexcept override;
};

}  // namespace mister::svc
