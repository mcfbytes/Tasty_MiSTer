// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/ps2_frame.h"

namespace mister::cores::minimig {

struct AmigaKeyCode {
    static constexpr std::uint8_t kReleaseBit = 0x80;

    std::uint8_t v = 0;

    [[nodiscard]] constexpr AmigaKeyCode released() const noexcept {
        return AmigaKeyCode{static_cast<std::uint8_t>(v | kReleaseBit)};
    }

    [[nodiscard]] constexpr proto::Ps2Frame frame() const noexcept {
        proto::Ps2Frame out{.stream = proto::Ps2Stream::KeyboardFrame, .len = 1};
        out.bytes[0] = v;
        return out;
    }

    friend constexpr bool operator==(AmigaKeyCode, AmigaKeyCode) = default;
};

}
