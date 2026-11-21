// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace mister::cores::n64 {

struct ByteRun {
    std::size_t at = 0;
    std::size_t len = 0;
    friend constexpr bool operator==(ByteRun, ByteRun) = default;
};

struct ByteMask {
    std::uint8_t v = 0;

    static constexpr std::size_t kBytes = 4;

    static constexpr std::uint8_t kNibbleMask = 0x0fu;

    [[nodiscard]] static constexpr ByteMask of_nibble(std::uint8_t nibble) noexcept {
        return ByteMask{static_cast<std::uint8_t>(nibble & kNibbleMask)};
    }

    [[nodiscard]] constexpr std::span<std::uint8_t> reach(
        std::span<std::uint8_t, kBytes> lanes
    ) const noexcept {
        std::size_t n = 0;
        for (std::size_t i = 0; i < kBytes; ++i) {
            if (lane_set_(i))
                n = i + 1;
        }
        return std::span<std::uint8_t>(lanes).first(n);
    }

    template <class F>
    constexpr void for_each_masked(
        std::span<const std::uint8_t> mem,
        std::span<const std::uint8_t, kBytes> operand,
        F&& f
    ) const {
        for (std::size_t i = 0; i < kBytes && i < mem.size(); ++i) {
            const bool set = lane_set_(i);
            f(set ? mem[i] : std::uint8_t{0}, set ? operand[i] : std::uint8_t{0});
        }
    }

    [[nodiscard]] constexpr bool covers_all() const noexcept { return v == kNibbleMask; }

    template <class F>
    constexpr void for_each_run(F&& f) const {
        for (std::size_t i = 0; i < kBytes;) {
            std::size_t n = 0;
            while (i + n < kBytes && lane_set_(i + n))
                ++n;
            if (n == 0) {
                ++i;
                continue;
            }
            f(ByteRun{i, n});
            i += n;
        }
    }

    friend constexpr bool operator==(ByteMask, ByteMask) = default;

private:
    [[nodiscard]] constexpr bool lane_set_(std::size_t i) const noexcept {
        return (v >> i & 1u) != 0;
    }
};

}
