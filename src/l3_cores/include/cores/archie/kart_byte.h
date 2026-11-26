// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/types.h"
#include "proto/ps2_frame.h"

namespace mister::cores::archie {

struct KartByte {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr KartByte of(hal::LinkWord word) noexcept {
        return KartByte{word.low_byte()};
    }

    [[nodiscard]] constexpr KartByte with(KartByte nibble) const noexcept {
        return KartByte{static_cast<std::uint8_t>(v | nibble.v)};
    }

    [[nodiscard]] constexpr proto::Ps2Frame frame() const noexcept {
        proto::Ps2Frame out{.stream = proto::Ps2Stream::KeyboardFrame, .len = 1};
        out.bytes[0] = v;
        return out;
    }

    friend constexpr bool operator==(KartByte, KartByte) = default;
};

}
