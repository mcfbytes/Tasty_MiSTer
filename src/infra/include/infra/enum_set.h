// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <type_traits>

#include "infra/message_sum.h"
#include "infra/seat.h"

namespace mister::infra {

namespace detail {

template <std::size_t N>
using SmallestRep = std::conditional_t<
    N <= 8,
    std::uint8_t,
    std::conditional_t<
        N <= 16,
        std::uint16_t,
        std::conditional_t<N <= 32, std::uint32_t, std::uint64_t>>>;
}

template <class E, class Rep = detail::SmallestRep<ordinal(E::kCount)>>
class EnumSet {
    TASTY_SEAT_EXEMPT(component);

public:
    static_assert(std::is_enum_v<E>, "EnumSet is keyed by an enum");
    static_assert(
        ordinal(E::kCount) <= std::numeric_limits<Rep>::digits,
        "EnumSet's Rep holds one bit per enumerator below kCount"
    );

    constexpr EnumSet() noexcept = default;

    [[nodiscard]] static consteval EnumSet of(std::initializer_list<E> members) noexcept {
        EnumSet out{};
        for (const E member : members) {
            if (ordinal(member) >= ordinal(E::kCount))
                not_a_member();
            out.insert(member);
        }
        return out;
    }

    [[nodiscard]] static constexpr EnumSet all() noexcept { return EnumSet{all_bits()}; }

    [[nodiscard]] static constexpr EnumSet from_bits(Rep bits) noexcept {
        return EnumSet{static_cast<Rep>(bits & all_bits())};
    }

    constexpr void insert(E member) noexcept { bits_ = static_cast<Rep>(bits_ | bit(member)); }

    constexpr void remove(E member) noexcept {
        bits_ = static_cast<Rep>(bits_ & static_cast<Rep>(~bit(member)));
    }

    [[nodiscard]] constexpr bool contains(E member) const noexcept {
        return (bits_ & bit(member)) != 0;
    }

    [[nodiscard]] constexpr bool is_empty() const noexcept { return bits_ == 0; }

    [[nodiscard]] constexpr EnumSet united(EnumSet other) const noexcept {
        return EnumSet{static_cast<Rep>(bits_ | other.bits_)};
    }

    [[nodiscard]] constexpr EnumSet intersection(EnumSet other) const noexcept {
        return EnumSet{static_cast<Rep>(bits_ & other.bits_)};
    }

    [[nodiscard]] constexpr EnumSet complement() const noexcept {
        return EnumSet{static_cast<Rep>(static_cast<Rep>(~bits_) & all_bits())};
    }

    [[nodiscard]] constexpr bool is_subset(EnumSet other) const noexcept {
        return (bits_ & static_cast<Rep>(~other.bits_)) == 0;
    }

    [[nodiscard]] constexpr Rep bits() const noexcept { return bits_; }

    friend constexpr bool operator==(EnumSet, EnumSet) noexcept = default;

private:
    constexpr explicit EnumSet(Rep bits) noexcept : bits_(bits) {}

    static void not_a_member() noexcept;

    [[nodiscard]] static constexpr Rep bit(E member) noexcept {
        return static_cast<Rep>(Rep{1} << ordinal(member));
    }

    [[nodiscard]] static constexpr Rep all_bits() noexcept {
        if constexpr (ordinal(E::kCount) >= std::numeric_limits<Rep>::digits) {
            return static_cast<Rep>(~Rep{0});
        } else {
            return static_cast<Rep>((Rep{1} << ordinal(E::kCount)) - Rep{1});
        }
    }

    Rep bits_ = 0;
};

}
