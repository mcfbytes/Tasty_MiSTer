// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "infra/byte_count.h"
#include "infra/byte_offset.h"

namespace mister::cores::minimig {

inline constexpr infra::ByteCount kSharePageBytes{0x2000};
inline constexpr infra::ByteOffset kShareFlagAt{0x0000};
inline constexpr infra::ByteOffset kSharePacketAt{0x0004};
inline constexpr infra::ByteOffset kShareDataAt{0x1000};
inline constexpr infra::ByteCount kShareDataBytes{0x1000};

inline constexpr infra::ByteCount kSharePacketBytes = kShareDataAt - kSharePacketAt;
static_assert(kShareDataAt + kShareDataBytes == kShareFlagAt + kSharePageBytes);

inline constexpr std::string_view kShareDevice = "SHARE";
inline constexpr std::string_view kShareVolume = "MiSTer";

inline constexpr std::string_view kShareDefaultDir = "shared";
inline constexpr std::string_view kShareRootName = "MiSTer";

enum class ShareAction : std::int32_t {
    Nil = 0,
    LocateObject = 8,
    FreeLock = 15,
    DeleteObject = 16,
    RenameObject = 17,
    CopyLock = 19,
    SetProtect = 21,
    CreateDir = 22,
    ExamineObject = 23,
    ExamineNext = 24,
    DiskInfo = 25,
    Info = 26,
    SetComment = 28,
    Parent = 29,
    SameLock = 40,
    Read = 'R',
    Write = 'W',
    FindUpdate = 1004,
    FindInput = 1005,
    FindOutput = 1006,
    End = 1007,
    Seek = 1008,
    ExamineFh = 1034,
};

enum class ShareError : std::int32_t {
    None = 0,
    LockDifferent = -1,
    LockSameVolume = 1,
    ObjectInUse = 202,
    ObjectExists = 203,
    ObjectNotFound = 205,
    ActionNotKnown = 209,
    ObjectWrongType = 212,
    DirectoryNotEmpty = 216,
    SeekError = 219,
    NoMoreEntries = 232,
};

enum class ShareEntry : std::int32_t { None = 0, UserDir = 2, File = -3 };

enum class ShareSeek : std::int32_t { Beginning = -1, Current = 0, End = 1 };

}
