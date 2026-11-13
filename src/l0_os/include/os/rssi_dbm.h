// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/decimal_text.h"
#include "infra/seat.h"
#include "os/types.h"

namespace mister::os {

class RssiDbm {
    TASTY_SEAT_EXEMPT(component);

public:
    [[nodiscard]] static constexpr RssiDbm of(std::int16_t dbm) noexcept { return RssiDbm{dbm}; }

    [[nodiscard]] static constexpr RssiDbm of_wire(WireI16 dbm) noexcept {
        return RssiDbm{static_cast<std::int16_t>(dbm.v)};
    }

    [[nodiscard]] constexpr std::int16_t dbm() const noexcept { return v; }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    friend constexpr bool operator==(const RssiDbm&, const RssiDbm&) = default;

private:
    constexpr RssiDbm() noexcept = default;

    constexpr explicit RssiDbm(std::int16_t dbm) noexcept : v(dbm) {}

    std::int16_t v = 0;
};

static_assert(std::is_trivially_copyable_v<RssiDbm>);
static_assert(std::has_unique_object_representations_v<RssiDbm>);
static_assert(sizeof(RssiDbm) == sizeof(std::int16_t));

}
