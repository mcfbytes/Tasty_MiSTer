// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

#include "infra/fixed_str.h"
#include "infra/seat.h"

namespace mister::cores::mra {

using Md5Hex = FixedStr<33, StrFit::Reject>;

class Md5 {
    TASTY_SEAT_EXEMPT(component);

public:
    Md5() noexcept;
    void update(std::span<const std::uint8_t> data) noexcept;
    [[nodiscard]] std::array<std::uint8_t, 16> digest() noexcept;
    [[nodiscard]] Md5Hex hex_str() noexcept;
    [[nodiscard]] std::string hex_digest() noexcept;

private:
    void block(const std::uint8_t* p) noexcept;

    std::array<std::uint32_t, 4> h_{};
    std::uint64_t total_ = 0;
    std::array<std::uint8_t, 64> tail_{};
};

}  // namespace mister::cores::mra
