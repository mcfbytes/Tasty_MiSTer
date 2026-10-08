// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <optional>
#include <type_traits>

#include "infra/error.h"
#include "infra/seat.h"

namespace mister::infra {

template <class T>
class OptRef {
    TASTY_SEAT_EXEMPT(component);
    static_assert(!std::is_reference_v<T>, "OptRef<T> names the referee, never T&");

public:
    constexpr OptRef() noexcept = default;
    constexpr OptRef(std::nullopt_t) noexcept {}
    constexpr explicit OptRef(T& ref) noexcept : ptr_(std::addressof(ref)) {}
    OptRef(T&&) = delete;

    constexpr OptRef& operator=(std::nullopt_t) noexcept {
        ptr_ = nullptr;
        return *this;
    }

    [[nodiscard]] constexpr bool has_value() const noexcept { return ptr_ != nullptr; }
    constexpr explicit operator bool() const noexcept { return ptr_ != nullptr; }

    [[nodiscard]] constexpr T& operator*() const noexcept { return *checked_(); }
    [[nodiscard]] constexpr T* operator->() const noexcept { return checked_(); }

    [[nodiscard]] constexpr T& value() const noexcept {
        if (ptr_ == nullptr)
            fatal(Error{Errc::not_found, ERR_SITE(), 0}, "OptRef::value() on empty");
        return *ptr_;
    }

    constexpr void reset() noexcept { ptr_ = nullptr; }

private:
    [[nodiscard]] constexpr T* checked_() const noexcept {
        if constexpr (kSeatChecksEnabled)
            if (ptr_ == nullptr)
                fatal(Error{Errc::not_found, ERR_SITE(), 0}, "OptRef dereferenced empty");
        return ptr_;
    }

    T* ptr_ = nullptr;
};

template <class T>
[[nodiscard]] constexpr OptRef<T> opt_ref_of(T* p) noexcept {
    return p != nullptr ? OptRef<T>{*p} : OptRef<T>{};
}

static_assert(std::is_trivially_copyable_v<OptRef<int>>);
static_assert(sizeof(OptRef<int>) == sizeof(int*), "one pointer wide");

}  // namespace mister::infra
