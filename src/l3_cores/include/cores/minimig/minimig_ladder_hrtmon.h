// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/minimig/minimig_config.h"
#include "cores/minimig/minimig_ladder_bus_bytes.h"
#include "infra/endian.h"
#include "proto/types.h"

namespace mister::cores::minimig::hrtmon {

struct Machine {
    bool ide = false;
    bool aga = false;
    bool ntsc = false;
};

inline constexpr BusBytes kConfigAt{20};
inline constexpr BusBytes kMaxChipAt{68};

inline constexpr proto::FileSize kLargestRom{512u * 1024u};

inline constexpr std::size_t kConfigBytes = 22;
using ConfigBlock = std::array<std::byte, kConfigBytes>;
using MaxChip = std::array<std::byte, sizeof(std::uint32_t)>;

[[nodiscard]] constexpr ConfigBlock config_block(Machine machine) noexcept {
    constexpr std::uint32_t kMonSize = 0x00800000;
    constexpr std::byte kOn{0xFF};
    constexpr std::byte kOff{0x00};
    ConfigBlock b{};
    infra::store_be<std::uint32_t>(
        std::span<std::byte>(b).first<sizeof(std::uint32_t)>(),
        kMonSize
    );
    std::size_t at = sizeof(std::uint32_t);
    for (const std::byte v : {
             kOff,
             std::byte{0x5A},
             std::byte{0x0F},
             kOn,
             kOn,
             kOff,
             kOn,
             machine.ide ? kOn : kOff,
             kOn,
             machine.aga ? kOn : kOff,
             kOn,
             std::byte{0x0F},
             kOn,
             kOff,
             machine.ntsc ? std::byte{1} : kOff,
             kOn,
             kOff,
             std::byte{1},
         }) {
        b[at++] = v;
    }
    return b;
}

[[nodiscard]] inline ConfigBlock config_block(const MinimigConfig& cfg) noexcept {
    return config_block(Machine{.ide = cfg.ide_enabled(), .aga = cfg.aga(), .ntsc = cfg.ntsc()});
}

[[nodiscard]] inline MaxChip max_chip(const MinimigConfig& cfg) noexcept {
    return cfg.chip_ram_top().long_bytes();
}

static_assert(
    config_block(Machine{.ide = true, .ntsc = true})[11] == std::byte{0xFF}
        && config_block(Machine{.ide = true, .ntsc = true})[13] == std::byte{0x00}
        && config_block(Machine{.ide = true, .ntsc = true})[18] == std::byte{0x01}
        && config_block(Machine{.aga = true})[21] == std::byte{0x01},
    "minimig_config.cpp:303-343: ide is the 8th switch byte, aga the 10th, the "
    "screen mode the 15th, hexmode the last"
);
static_assert(
    kMaxChipAt - kConfigAt >= BusBytes::of(ConfigBlock{}),
    "the two records do not overlap"
);

}
