// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <utility>

#include "hal/types.h"

namespace mister::proto {

enum class FioOp : std::uint16_t {
    FileTx = 0x53,
    FileTxDat = 0x54,
    FileIndex = 0x55,
    FileInfo = 0x56,
    SharpmzFileAddr = 0x57,
    SharpmzFileAddrTx = 0x58,
    SharpmzFileAddrRx = 0x59,
    SharpmzConfigRx = 0x5A,
    SharpmzConfigTx = 0x5B,
};

[[nodiscard]] constexpr hal::SpiWord word(FioOp op) noexcept {
    return hal::SpiWord{static_cast<std::uint16_t>(op)};
}

static_assert(word(FioOp::FileTx) == hal::SpiWord{0x53});
static_assert(word(FioOp::FileTxDat) == hal::SpiWord{0x54});
static_assert(word(FioOp::FileIndex) == hal::SpiWord{0x55});
static_assert(word(FioOp::FileInfo) == hal::SpiWord{0x56});
static_assert(word(FioOp::SharpmzFileAddr) == hal::SpiWord{0x57});
static_assert(word(FioOp::SharpmzFileAddrTx) == hal::SpiWord{0x58});
static_assert(word(FioOp::SharpmzFileAddrRx) == hal::SpiWord{0x59});
static_assert(word(FioOp::SharpmzConfigRx) == hal::SpiWord{0x5A});
static_assert(word(FioOp::SharpmzConfigTx) == hal::SpiWord{0x5B});

}
