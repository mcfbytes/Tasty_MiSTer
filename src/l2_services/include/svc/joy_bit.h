// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "svc/types.h"

namespace mister::svc {

struct JoyBit {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr JoyBit next() const noexcept {
        return JoyBit{static_cast<std::uint8_t>(v + 1u)};
    }

    [[nodiscard]] constexpr JoyMask mask() const noexcept { return JoyMask{1u << v}; }

    friend constexpr bool operator==(JoyBit, JoyBit) = default;
    friend constexpr auto operator<=>(JoyBit, JoyBit) = default;
};

inline constexpr JoyBit kJoyBitRight{0};
inline constexpr JoyBit kJoyBitLeft{1};
inline constexpr JoyBit kJoyBitDown{2};
inline constexpr JoyBit kJoyBitUp{3};
inline constexpr JoyBit kJoyBitBtn1{4};
inline constexpr JoyBit kJoyBitBtn2{5};
inline constexpr JoyBit kJoyBitBtn3{6};
inline constexpr JoyBit kJoyBitBtn4{7};
inline constexpr JoyBit kJoyBitL{10};
inline constexpr JoyBit kJoyBitR{11};
inline constexpr JoyBit kJoyBitL2{12};
inline constexpr JoyBit kJoyBitR2{13};

inline constexpr JoyBit kJoyBitEnd{32};

}
