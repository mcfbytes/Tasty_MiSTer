// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores::st_ctrl {

inline constexpr std::uint32_t kCpuReset = 0x00000001u;
inline constexpr std::uint32_t kMemConfigMask = 0x0000000Eu;
inline constexpr std::uint32_t kMemConfig1M = 1u << 1;
inline constexpr std::uint32_t kMemConfig8M = 4u << 1;
inline constexpr std::uint32_t kFdcWriteProtectA = 0x00000040u;
inline constexpr std::uint32_t kFdcWriteProtectB = 0x00000080u;
inline constexpr std::uint32_t kVideoColor = 0x00000100u;
inline constexpr std::uint32_t kVideoAspect1 = 0x00000200u;
inline constexpr std::uint32_t kAcsi0Enable = 0x00000400u;
inline constexpr std::uint32_t kTurbo = 0x00040000u;
inline constexpr std::uint32_t kBlitter = 0x00080000u;
inline constexpr std::uint32_t kScanlinesMask = 0x00300000u;
inline constexpr std::uint32_t kStereo = 0x00400000u;
inline constexpr std::uint32_t kSte = 0x00800000u;
inline constexpr std::uint32_t kChipsetMask = 0x01800000u;
inline constexpr std::uint32_t kDongle = 0x02000000u;
inline constexpr std::uint32_t kViking = 0x10000000u;
inline constexpr std::uint32_t kBorder = 0x20000000u;
inline constexpr std::uint32_t kMde60 = 0x40000000u;
inline constexpr std::uint32_t kVideoAspect2 = 0x80000000u;

inline constexpr std::uint32_t kExtCrop216 = 0x00000400u;

inline constexpr std::uint32_t kDefaultSystemCtrl = kMemConfig1M | kVideoColor | kBorder;

[[nodiscard]] constexpr std::uint32_t write_protect_bit(std::uint8_t index) noexcept {
    return index == 0 ? kFdcWriteProtectA : kFdcWriteProtectB;
}

}
