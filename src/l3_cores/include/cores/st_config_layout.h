// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace mister::cores::st_cfg {

inline constexpr std::size_t kPathBytes = 1024;

inline constexpr std::size_t kSystemCtrl = 0;
inline constexpr std::size_t kTosImg = kSystemCtrl + 4;
inline constexpr std::size_t kCartImg = kTosImg + kPathBytes;
inline constexpr std::size_t kAcsiImg0 = kCartImg + kPathBytes;
inline constexpr std::size_t kAcsiImg1 = kAcsiImg0 + kPathBytes;
inline constexpr std::size_t kVideoAdjust = kAcsiImg1 + kPathBytes;
inline constexpr std::size_t kCdcRedirect = kVideoAdjust + 2;
inline constexpr std::size_t kReserved = kCdcRedirect + 1;
inline constexpr std::size_t kExtCtrl = kReserved + 1;
inline constexpr std::size_t kBytes = kExtCtrl + 4;

static_assert(kBytes == 4108, "sizeof(tos_config_t) on the armhf target");

inline constexpr const char* kFileStem = "ATARIST";
inline constexpr const char* kFileExt = ".CFG";
inline constexpr std::uint8_t kSlotCount = 9;

}
