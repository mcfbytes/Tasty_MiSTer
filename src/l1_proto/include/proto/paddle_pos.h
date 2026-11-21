// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <limits>
#include <optional>

#include "proto/spinner_travel.h"
#include "proto/stick_pos.h"

namespace mister::proto {

struct PaddlePos {
    std::uint8_t v = 0;

    class Range {
    public:
        class Low {
        public:
            [[nodiscard]] constexpr std::optional<Range> to(std::int32_t hi) const noexcept {
                if (hi <= lo_)
                    return std::nullopt;
                Range out;
                out.lo_ = lo_;
                out.hi_ = hi;
                return out;
            }

        private:
            friend class Range;

            constexpr explicit Low(std::int32_t lo) noexcept : lo_{lo} {}

            std::int32_t lo_;
        };

        [[nodiscard]] static constexpr Low from(std::int32_t lo) noexcept { return Low{lo}; }

        friend constexpr bool operator==(const Range&, const Range&) = default;

    private:
        friend struct PaddlePos;
        constexpr Range() noexcept = default;
        std::int32_t lo_ = 0;
        std::int32_t hi_ = 0;
    };

    [[nodiscard]] static constexpr PaddlePos scaled(std::int32_t raw, Range range) noexcept {
        std::int32_t value = raw;
        if (value < range.lo_)
            value = range.lo_;
        else if (value > range.hi_)
            value = range.hi_;
        constexpr std::int64_t top = std::numeric_limits<std::uint8_t>::max();
        const std::int64_t num = (std::int64_t{value} - range.lo_) * top;
        const std::int64_t den = std::int64_t{range.hi_} - range.lo_;
        return PaddlePos{static_cast<std::uint8_t>(num / den)};
    }

    [[nodiscard]] constexpr PaddlePos turned(SpinnerTravel by) const noexcept {
        constexpr std::int64_t top = std::numeric_limits<std::uint8_t>::max();
        const std::int64_t to = std::int64_t{v} + by.v;
        if (to < 0)
            return PaddlePos{0};
        if (to > top)
            return PaddlePos{std::numeric_limits<std::uint8_t>::max()};
        return PaddlePos{static_cast<std::uint8_t>(to)};
    }

    [[nodiscard]] constexpr StickPos stick() const noexcept { return StickPos{v, 0}; }

    friend constexpr bool operator==(PaddlePos, PaddlePos) = default;
};

static_assert(
    PaddlePos::scaled(1023, PaddlePos::Range::from(0).to(1023).value()) == PaddlePos{255},
    "the high stop of the range is the top of the byte"
);
static_assert(
    !PaddlePos::Range::from(7).to(7) && !PaddlePos::Range::from(9).to(3),
    "an empty or reversed range is no range"
);
static_assert(PaddlePos{250}.turned(SpinnerTravel{100}) == PaddlePos{255}, "stops at the end");
static_assert(PaddlePos{0}.turned(SpinnerTravel{200}) == PaddlePos{200}, "the travel, uncut");

}
