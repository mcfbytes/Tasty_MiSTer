// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/spi_sample.h"
#include "hal/types.h"

namespace mister::hal {

struct LwPresence {

    [[nodiscard]] constexpr bool lets_discover() const noexcept {
        return test(kInUserMode) && test(kPresent) && test(kIdle);
    }

    [[nodiscard]] constexpr bool firewall_held() const noexcept {
        return test(kPresent) && !test(kIdle);
    }

    [[nodiscard]] constexpr bool enabled() const noexcept { return test(kEnQ); }

    [[nodiscard]] constexpr bool in_user_mode() const noexcept { return test(kInUserMode); }

    [[nodiscard]] constexpr bool present() const noexcept { return test(kPresent); }

    [[nodiscard]] static constexpr LwPresence of(GpiWord gpi) noexcept {
        return LwPresence{static_cast<std::uint8_t>(
            (gpi.test(kGpiNotReadyField) ? 0u : kInUserMode)
            | (gpi.test(kGpiLwPresentField) ? kPresent : 0u)
            | (gpi.test(kGpiLwIdleField) ? kIdle : 0u)
            | (gpi.test(kGpiLwEnabledField) ? kEnQ : 0u)
        )};
    }

    friend constexpr bool operator==(LwPresence, LwPresence) = default;

    LwPresence() = default;

private:
    static constexpr std::uint8_t kInUserMode = 0x1;
    static constexpr std::uint8_t kPresent = 0x2;
    static constexpr std::uint8_t kIdle = 0x4;
    static constexpr std::uint8_t kEnQ = 0x8;

    explicit constexpr LwPresence(std::uint8_t bits) noexcept : bits_(bits) {}

    [[nodiscard]] constexpr bool test(std::uint8_t bit) const noexcept {
        return (bits_ & bit) != 0u;
    }

    std::uint8_t bits_ = 0;
};

static_assert(sizeof(LwPresence) == 1);

}
