// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/error.h"
#include "infra/seat.h"

namespace mister::os {

class DbusSerial {
    TASTY_SEAT_EXEMPT(component);

public:
    constexpr DbusSerial() noexcept = default;

    [[nodiscard]] static Ex<DbusSerial> of(std::uint32_t value) noexcept {
        if (value == 0)
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
        return DbusSerial{value};
    }

    [[nodiscard]] constexpr std::uint32_t value() const noexcept { return value_; }

    [[nodiscard]] static constexpr DbusSerial hello() noexcept { return DbusSerial{kHello}; }

    [[nodiscard]] static constexpr DbusSerial first_user() noexcept {
        return DbusSerial{kFirstUser};
    }

    [[nodiscard]] constexpr DbusSerial next() const noexcept {
        const std::uint32_t following = value_ + 1;
        return following == 0 ? DbusSerial{kFirst} : DbusSerial{following};
    }

    friend constexpr bool operator==(const DbusSerial&, const DbusSerial&) = default;

private:
    enum : std::uint32_t { kHello = 1, kFirst = 1, kFirstUser = 2 };

    constexpr explicit DbusSerial(std::uint32_t value) noexcept : value_(value) {}

    std::uint32_t value_ = 0;
};

static_assert(std::is_trivially_copyable_v<DbusSerial>);
static_assert(std::has_unique_object_representations_v<DbusSerial>);
static_assert(sizeof(DbusSerial) == sizeof(std::uint32_t));

}
