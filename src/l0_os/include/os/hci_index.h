// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/error.h"
#include "infra/seat.h"

namespace mister::os {

class HciIndex {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::uint16_t kCount = 16;

    [[nodiscard]] static constexpr Ex<HciIndex> of(std::uint16_t index) noexcept {
        if (index >= kCount)
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), index});
        return HciIndex{index};
    }

    [[nodiscard]] static constexpr HciIndex first() noexcept { return HciIndex{0}; }

    [[nodiscard]] constexpr std::uint16_t value() const noexcept { return index_; }

    [[nodiscard]] constexpr int ioctl_arg() const noexcept { return index_; }

    friend constexpr bool operator==(const HciIndex&, const HciIndex&) = default;

private:
    constexpr explicit HciIndex(std::uint16_t index) noexcept : index_(index) {}

    std::uint16_t index_ = 0;
};

static_assert(std::is_trivially_copyable_v<HciIndex>);
static_assert(std::has_unique_object_representations_v<HciIndex>);

}
