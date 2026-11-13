// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/decimal_text.h"
#include "infra/seat.h"
#include "os/signal_bars.h"

namespace mister::os {

class SignalDbm {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::int16_t kBar4Min = -50;
    static constexpr std::int16_t kBar3Min = -60;
    static constexpr std::int16_t kBar2Min = -75;
    static constexpr std::int16_t kBar1Min = -85;

    [[nodiscard]] static constexpr SignalDbm of(std::int16_t dbm) noexcept {
        return SignalDbm{dbm};
    }

    [[nodiscard]] constexpr std::int16_t dbm() const noexcept { return dbm_; }

    [[nodiscard]] constexpr SignalBars bars() const noexcept {
        if (dbm_ >= kBar4Min)
            return SignalBars::of(SignalBars::kMax);
        if (dbm_ >= kBar3Min)
            return SignalBars::of(SignalBars::kMax - 1);
        if (dbm_ >= kBar2Min)
            return SignalBars::of(SignalBars::kMax - 2);
        if (dbm_ >= kBar1Min)
            return SignalBars::of(SignalBars::kMax - 3);
        return SignalBars::of(0);
    }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{dbm_}; }

    friend constexpr bool operator==(const SignalDbm&, const SignalDbm&) = default;

private:
    constexpr SignalDbm() noexcept = default;

    constexpr explicit SignalDbm(std::int16_t dbm) noexcept : dbm_(dbm) {}

    std::int16_t dbm_ = 0;
};

static_assert(std::is_trivially_copyable_v<SignalDbm>);
static_assert(std::has_unique_object_representations_v<SignalDbm>);
static_assert(sizeof(SignalDbm) == sizeof(std::int16_t));

}
