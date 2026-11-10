// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace mister::cores {

class SegaCddEngine;

inline constexpr unsigned kFrameNibbleBits = 4;
inline constexpr std::uint8_t kFrameNibbleMask = 0x0F;

struct SegaFrameWord {
    std::uint64_t v = 0;
    friend constexpr bool operator==(SegaFrameWord, SegaFrameWord) = default;

private:
    friend class SegaCddEngine;

    [[nodiscard]] constexpr std::uint8_t nibble(std::size_t at) const noexcept {
        return static_cast<std::uint8_t>((v >> (kFrameNibbleBits * at)) & kFrameNibbleMask);
    }
};

}
