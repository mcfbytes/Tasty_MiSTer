// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <utility>

#include "proto/types.h"

namespace mister::cores::zx81 {

enum class SidecarClass : std::uint8_t { Colour = 1, Chars = 3 };

[[nodiscard]] constexpr proto::WideIoIndex sidecar_index(
    proto::FioIndexByte tape,
    SidecarClass c
) noexcept {
    constexpr unsigned kRowBits = 0x1F;
    constexpr unsigned kClassShift = 5;
    const unsigned row = tape.wire_word().low_byte() & kRowBits;
    const unsigned word = row | (static_cast<unsigned>(std::to_underlying(c)) << kClassShift);
    return proto::WideIoIndex::of(proto::FioIndexByte{static_cast<std::uint8_t>(word)});
}

static_assert(
    sidecar_index(proto::FioIndexByte{0x41}, SidecarClass::Colour) == proto::WideIoIndex{0x21}
        && sidecar_index(proto::FioIndexByte{0x41}, SidecarClass::Chars)
            == proto::WideIoIndex{0x61},
    "user_io.cpp:2947-2948: (index & 0x1F) | 0x20 and | 0x60"
);

}
