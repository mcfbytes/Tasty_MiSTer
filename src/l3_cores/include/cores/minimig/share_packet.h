// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "infra/byte_count.h"
#include "infra/byte_offset.h"

namespace mister::cores::minimig {

inline constexpr infra::ByteCount kWord{sizeof(std::uint32_t)};

inline constexpr infra::ByteOffset kSizeAt{0};
inline constexpr infra::ByteOffset kTypeAt{4};
inline constexpr infra::ByteOffset kKeyAt{8};
inline constexpr infra::ByteOffset kSuccessAt = kTypeAt;
inline constexpr infra::ByteOffset kErrorAt = kKeyAt;
inline constexpr infra::ByteOffset kSecondAt = kKeyAt + kWord;
inline constexpr infra::ByteOffset kThirdAt = kSecondAt + kWord;
inline constexpr infra::ByteOffset kBodyAt = kSecondAt;

inline constexpr infra::ByteCount kHeaderBytes = kBodyAt - infra::ByteOffset{};

inline constexpr infra::ByteOffset kLocateNameAt = kThirdAt + infra::ByteCount{1};
inline constexpr infra::ByteOffset kObjectNameAt = kSecondAt + infra::ByteCount{1};
inline constexpr infra::ByteOffset kRenameLengthAt = kThirdAt;
inline constexpr infra::ByteOffset kRenameNameAt = kRenameLengthAt + infra::ByteCount{2};

inline constexpr infra::ByteOffset kExamineTypeAt = kBodyAt + kWord;
inline constexpr infra::ByteOffset kExamineSizeAt = kExamineTypeAt + kWord;
inline constexpr infra::ByteOffset kExamineProtectAt = kExamineSizeAt + kWord;
inline constexpr infra::ByteOffset kExamineDateAt = kExamineProtectAt + kWord;
inline constexpr infra::ByteOffset kExamineMinutesAt = kExamineDateAt + kWord;
inline constexpr infra::ByteOffset kExamineTicksAt = kExamineMinutesAt + kWord;
inline constexpr infra::ByteOffset kExamineNameAt = kExamineTicksAt + kWord;
inline constexpr infra::ByteOffset kVolumeUsedAt = kBodyAt + kWord;

inline constexpr infra::ByteCount kGenericBytes{12};
inline constexpr infra::ByteCount kWordBytes{16};
inline constexpr infra::ByteCount kExamineBytes{41};
inline constexpr infra::ByteCount kVolumeBytes{24};

}
