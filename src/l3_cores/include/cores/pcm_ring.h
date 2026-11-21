// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "hal/spi_halves.h"
#include "hal/types.h"

namespace mister::cores {

struct PcmRingPoint {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr PcmRingPoint of(hal::SpiWord w) noexcept {
        return w.decoded([](std::uint16_t d) { return PcmRingPoint{d}; });
    }

    [[nodiscard]] constexpr hal::SpiWord wire_word() const noexcept {
        return hal::SpiHalves::of(v).low;
    }

    [[nodiscard]] constexpr std::size_t index() const noexcept { return v; }

    friend constexpr bool operator==(PcmRingPoint, PcmRingPoint) = default;
};

struct PcmRingCursors {
    PcmRingPoint write;
    PcmRingPoint read;
};

class PcmRing {
public:
    constexpr PcmRing() noexcept = default;

    [[nodiscard]] static constexpr PcmRing of_length(std::size_t bytes) noexcept {
        PcmRing r;
        r.len_ = static_cast<std::uint32_t>(bytes);
        return r;
    }

    [[nodiscard]] constexpr std::uint32_t bytes() const noexcept { return len_; }

    [[nodiscard]] constexpr PcmRingPoint advanced(PcmRingPoint at, std::size_t n) const noexcept {
        return PcmRingPoint{static_cast<std::uint32_t>((at.v + n) & mask_())};
    }

    [[nodiscard]] constexpr std::size_t run_to_end(PcmRingPoint at) const noexcept {
        return len_ - at.v;
    }

    [[nodiscard]] constexpr std::uint32_t free_bytes(PcmRingCursors c) const noexcept {
        return (c.read.v - c.write.v - 1u) & mask_();
    }

private:
    [[nodiscard]] constexpr std::uint32_t mask_() const noexcept { return len_ - 1u; }

    std::uint32_t len_ = 0;
};

static_assert(PcmRingPoint{0x0001'2345u}.wire_word() == hal::SpiWord{0x2345});
static_assert(PcmRingPoint::of(hal::SpiWord{0xBEEF}) == PcmRingPoint{0xBEEFu});

}
