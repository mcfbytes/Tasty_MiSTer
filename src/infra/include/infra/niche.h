// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <type_traits>

#include "infra/error.h"
#include "infra/seat.h"

namespace mister::infra {

template <class T>
class Niche {
    TASTY_SEAT_EXEMPT(component);

    using Policy = typename T::NichePolicy;

    static_assert(std::is_trivially_copyable_v<T>, "a niche rides in a ring record");

public:
    constexpr Niche() noexcept : value_(Policy::none()) {
        static_assert(sizeof(Niche) == sizeof(T), "the niche adds no byte");
    }

    constexpr Niche(std::nullopt_t) noexcept : value_(Policy::none()) {}

    constexpr Niche(T value) noexcept : value_(value) {}

    constexpr Niche& operator=(std::nullopt_t) noexcept {
        reset();
        return *this;
    }

    [[nodiscard]] constexpr bool has_value() const noexcept { return !Policy::is_none(value_); }

    constexpr explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] constexpr T& operator*() noexcept { return checked_(); }

    [[nodiscard]] constexpr const T& operator*() const noexcept { return checked_(); }

    [[nodiscard]] constexpr T* operator->() noexcept { return &checked_(); }

    [[nodiscard]] constexpr const T* operator->() const noexcept { return &checked_(); }

    [[nodiscard]] constexpr T value() const noexcept {
        if (!has_value())
            fatal(Error{Errc::not_found, ERR_SITE(), 0}, "Niche::value() on empty");
        return value_;
    }

    [[nodiscard]] constexpr T value_or(T fallback) const noexcept {
        return has_value() ? value_ : fallback;
    }

    constexpr void reset() noexcept { value_ = Policy::none(); }

    friend constexpr bool operator==(const Niche&, const Niche&) = default;

private:
    [[nodiscard]] constexpr T& checked_() noexcept {
        if constexpr (kSeatChecksEnabled)
            if (!has_value())
                fatal(Error{Errc::not_found, ERR_SITE(), 0}, "Niche dereferenced empty");
        return value_;
    }

    [[nodiscard]] constexpr const T& checked_() const noexcept {
        if constexpr (kSeatChecksEnabled)
            if (!has_value())
                fatal(Error{Errc::not_found, ERR_SITE(), 0}, "Niche dereferenced empty");
        return value_;
    }

    T value_;
};

}
