// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"

namespace mister::app {

struct PlayRefusal {
    TASTY_SEAT_EXEMPT(component);

    enum class Why : std::uint8_t {
        None,
        Busy,
        NotWired,
        NoCodec,
        MovieIo,
        Movie,
        RomIo,
        RomSize,
        Checksum,
        Setting,
        Phase,
        Lead,
        Command,
        TooLong,
        Slot,
        PowerOn,
        Firmware,
        RomKind,
        RomChd,
        kCount,
    };

    Why why = Why::None;

    std::uint32_t detail = 0;

    [[nodiscard]] static const char* name(Why r) noexcept;
};

}
