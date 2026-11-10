// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "cores/neo_header.h"
#include "cores/romset_config.h"

namespace mister::cores::neogeo {

struct NeoQuirk {
    std::uint16_t id = 0;
    std::uint8_t hw = 0;
    std::uint8_t cmc = 0;
    std::uint8_t pvc = 0;
    std::uint8_t sma = 0;
    std::uint8_t xram = 0;
    std::uint8_t mir = 0;
    std::uint8_t rwait = 0;
    std::uint8_t pwait = 0;
};

inline constexpr std::size_t kNeoQuirkCount = 17;

extern const std::array<NeoQuirk, kNeoQuirkCount> kNeoQuirks;

struct CartQuirks {
    RomsetConfig config;
    bool vrom_mirror = true;
};

[[nodiscard]] CartQuirks resolve_quirks(const NeoHeader& hdr) noexcept;

}
