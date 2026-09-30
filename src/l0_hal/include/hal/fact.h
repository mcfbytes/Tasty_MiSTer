// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <cstdint>
#include <expected>

#include "infra/error.h"
#include "hal/unmeasured.h"

namespace mister::hal {

namespace detail {

void unmeasured_fact_read_in_a_constant_expression();
}

template <class T>
class Fact {
public:
    Fact() = delete;

    template <std::same_as<T> U>
    constexpr Fact(U v) noexcept : v_(v), measured_(true) {}
    constexpr Fact(Unmeasured) noexcept : v_{}, measured_(false) {}

    [[nodiscard]] constexpr bool measured() const noexcept { return measured_; }

    [[nodiscard]] consteval T value() const {
        if (!measured_) detail::unmeasured_fact_read_in_a_constant_expression();
        return v_;
    }

    [[nodiscard]] constexpr Ex<T> resolve(std::uint16_t site, std::uint32_t field) const noexcept {
        if (!measured_) return std::unexpected(Error{Errc::unimplemented, site, field});
        return v_;
    }

private:
    T v_;
    bool measured_;
};

}  // namespace mister::hal
