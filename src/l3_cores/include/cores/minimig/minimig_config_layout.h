// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "infra/affine.h"
#include "infra/byte_count.h"
#include "infra/byte_offset.h"

namespace mister::cores::minimig::mm_cfg {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;

inline constexpr infra::ByteCount kByte{1};
inline constexpr infra::ByteCount kHalf{2};
inline constexpr infra::ByteCount kIdBytes{8};
inline constexpr infra::ByteCount kKickstartBytes{992};
inline constexpr infra::ByteCount kLabelBytes{32};
inline constexpr infra::ByteCount kInfoBytes{64};
inline constexpr infra::ByteCount kDriveNameBytes{1024};

inline constexpr infra::ByteOffset kId{};
inline constexpr infra::ByteOffset kVersion = kId + kIdBytes;
inline constexpr infra::ByteOffset kExtCfgHigh = kVersion + kHalf;
inline constexpr infra::ByteOffset kKickstart = kExtCfgHigh + kHalf;
inline constexpr infra::ByteOffset kLabel = kKickstart + kKickstartBytes;
inline constexpr infra::ByteOffset kExtCfgLow = kLabel + kLabelBytes;
inline constexpr infra::ByteOffset kMemory = kExtCfgLow + kHalf;
inline constexpr infra::ByteOffset kChipset = kMemory + kByte;
inline constexpr infra::ByteOffset kFloppySpeed = kChipset + kByte;
inline constexpr infra::ByteOffset kFloppyDrives = kFloppySpeed + kByte;
inline constexpr infra::ByteOffset kDisableAr3 = kFloppyDrives + kByte;
inline constexpr infra::ByteOffset kIdeCfg = kDisableAr3 + kByte;
inline constexpr infra::ByteOffset kScanlines = kIdeCfg + kByte;
inline constexpr infra::ByteOffset kAudio = kScanlines + kByte;

inline constexpr infra::ByteCount kDriveName = kByte + kByte;
inline constexpr infra::ByteCount kDriveBytes = kDriveName + kDriveNameBytes;
inline constexpr infra::ByteOffset kHardfile0 = kAudio + kByte;
inline constexpr infra::ByteOffset kHardfile1 = kHardfile0 + kDriveBytes;
inline constexpr infra::ByteOffset kHardfile2 = kHardfile1 + kDriveBytes;
inline constexpr infra::ByteOffset kHardfile3 = kHardfile2 + kDriveBytes;
inline constexpr infra::ByteOffset kCpu = kHardfile3 + kDriveBytes;
inline constexpr infra::ByteOffset kAutofire = kCpu + kByte;
inline constexpr infra::ByteOffset kInfo = kAutofire + kByte;
inline constexpr infra::ByteOffset kCd32Drive = kInfo + kInfoBytes;
inline constexpr infra::ByteOffset kCdtvDrive = kCd32Drive + kDriveBytes;
inline constexpr infra::ByteOffset kNicMode = kCdtvDrive + kDriveBytes;

inline constexpr infra::ByteCount kBytes = kNicMode + kHalf - kId;

inline constexpr std::size_t kImageBytes = 7270;

static_assert(
    kKickstart == infra::ByteOffset{12}
    && kLabel == infra::ByteOffset{1004}
    && kExtCfgLow == infra::ByteOffset{1036}
    && kMemory == infra::ByteOffset{1038}
);
static_assert(
    kHardfile0 == infra::ByteOffset{1046}
    && kDriveBytes == infra::ByteCount{1026}
    && kCpu == infra::ByteOffset{5150}
    && kInfo == infra::ByteOffset{5152}
);
static_assert(
    kCd32Drive == infra::ByteOffset{5216}
    && kCdtvDrive == infra::ByteOffset{6242}
    && kNicMode == infra::ByteOffset{7268}
);
static_assert(kBytes == infra::ByteCount{kImageBytes}, "sizeof(mm_configTYPE) on the armhf target");

inline constexpr infra::ByteOffset kOldCpu = kHardfile2;
inline constexpr infra::ByteOffset kOldAutofire = kOldCpu + kByte;
inline constexpr infra::ByteCount kOldBytes = kOldAutofire + kByte - kId;
static_assert(kOldBytes == infra::ByteCount{3100}, "sizeof(configTYPE_old) on the armhf target");

inline constexpr std::string_view kFileId = "MNMGCFG0";
static_assert(infra::ByteCount{kFileId.size()} == kIdBytes);

inline constexpr std::string_view kFileStem = "Minimig";
inline constexpr std::string_view kFileExt = ".cfg";
inline constexpr std::uint8_t kSlotCount = 10;

inline constexpr std::string_view kHomeDir = "Amiga";

inline constexpr std::string_view kKickstartName = "KICK.ROM";
inline constexpr std::string_view kDefaultKickstart = "games/Amiga/KICK.ROM";
static_assert(
    kDefaultKickstart.ends_with(kKickstartName)
        && kDefaultKickstart.substr(0, kDefaultKickstart.size() - kKickstartName.size() - 1)
               .ends_with(kHomeDir),
    "the default kickstart is <games>/<home>/KICK.ROM"
);

}
