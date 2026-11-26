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

[[nodiscard]] constexpr hal::LinkWord word(FioOp op) noexcept {
    return hal::LinkWord{static_cast<std::uint16_t>(op)};
}

static_assert(word(FioOp::FileTx) == hal::LinkWord{0x53});
static_assert(word(FioOp::FileTxDat) == hal::LinkWord{0x54});
static_assert(word(FioOp::FileIndex) == hal::LinkWord{0x55});
static_assert(word(FioOp::FileInfo) == hal::LinkWord{0x56});
static_assert(word(FioOp::SharpmzFileAddr) == hal::LinkWord{0x57});
static_assert(word(FioOp::SharpmzFileAddrTx) == hal::LinkWord{0x58});
static_assert(word(FioOp::SharpmzFileAddrRx) == hal::LinkWord{0x59});
static_assert(word(FioOp::SharpmzConfigRx) == hal::LinkWord{0x5A});
static_assert(word(FioOp::SharpmzConfigTx) == hal::LinkWord{0x5B});

}
