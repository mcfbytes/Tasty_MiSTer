// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <limits>
#include <ratio>
#include <type_traits>

#include "app/rec_every.h"

namespace mister::app {

class FrameTicks {
public:
    static constexpr std::uint32_t kTickHz = 100'000'000;

    [[nodiscard]] static constexpr FrameTicks unknown() noexcept { return FrameTicks{}; }

    [[nodiscard]] static constexpr FrameTicks k60Hz() noexcept {
        FrameTicks r;
        r.v_ = k60HzTicks;
        return r;
    }

    [[nodiscard]] static constexpr FrameTicks plausible(std::uint32_t measured) noexcept {
        FrameTicks r;
        if (measured >= kMinTicks && measured <= kMaxTicks)
            r.v_ = measured;
        return r;
    }

    [[nodiscard]] constexpr bool known() const noexcept { return v_ != 0; }

    [[nodiscard]] constexpr FrameTicks or_60hz() const noexcept {
        return known() ? *this : k60Hz();
    }

    [[nodiscard]] constexpr bool same_rate_as(FrameTicks reference) const noexcept {
        if (!known() || !reference.known())
            return false;
        const std::uint64_t d = v_ > reference.v_ ? v_ - reference.v_ : reference.v_ - v_;
        return d * kSameRateDivisor <= reference.v_;
    }

    [[nodiscard]] constexpr std::chrono::nanoseconds period() const noexcept { return Ticks{v_}; }

    [[nodiscard]] constexpr std::uint32_t dw_scale(RecEvery mul) const noexcept {
        return v_ * mul.count<std::uint32_t>();
    }

    friend constexpr bool operator==(FrameTicks, FrameTicks) = default;

private:
    static constexpr std::uint32_t kMinTicks = kTickHz / 120;

    static constexpr std::uint32_t kMaxTicks = kTickHz / 20;
    static constexpr std::uint32_t k60HzTicks = 1'666'667;

    static constexpr std::uint32_t kSameRateDivisor = 1000;

    using Ticks = std::chrono::duration<std::int64_t, std::ratio<1, kTickHz>>;

    static_assert(k60HzTicks >= kMinTicks && k60HzTicks <= kMaxTicks);

    static_assert(
        std::uint64_t{kMaxTicks} * kRecEveryMax <= std::numeric_limits<std::uint32_t>::max()
    );
    std::uint32_t v_ = 0;
};

static_assert(std::is_trivially_copyable_v<FrameTicks>);
static_assert(std::is_standard_layout_v<FrameTicks>);
static_assert(std::has_unique_object_representations_v<FrameTicks>);
static_assert(sizeof(FrameTicks) == 4);
static_assert(std::nano::den % FrameTicks::kTickHz == 0);

}
