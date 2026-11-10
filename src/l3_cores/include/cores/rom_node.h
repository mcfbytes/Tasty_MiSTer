// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <variant>

#include "cores/file_part.h"
#include "cores/interleave_end.h"
#include "cores/interleave_start.h"
#include "cores/patch.h"
#include "cores/part_common.h"
#include "cores/text_part.h"

namespace mister::cores::mra {

using RomNode = std::variant<FilePart, TextPart, Patch, InterleaveStart, InterleaveEnd>;

enum class RomNodeKind : std::uint8_t {
    FilePart,
    TextPart,
    Patch,
    InterleaveStart,
    InterleaveEnd,
    kCount
};
inline constexpr std::size_t kRomNodeArms = static_cast<std::size_t>(RomNodeKind::kCount);
static_assert(std::variant_size_v<RomNode> == kRomNodeArms, "RomNode: one name per arm");
template <RomNodeKind K, class T>
inline constexpr bool kRomNodeArmIs = std::
    is_same_v<std::variant_alternative_t<static_cast<std::size_t>(K), RomNode>, T>;
static_assert(
    kRomNodeArmIs<RomNodeKind::FilePart, FilePart>
    && kRomNodeArmIs<RomNodeKind::TextPart, TextPart>
    && kRomNodeArmIs<RomNodeKind::Patch, Patch>
    && kRomNodeArmIs<RomNodeKind::InterleaveStart, InterleaveStart>
    && kRomNodeArmIs<RomNodeKind::InterleaveEnd, InterleaveEnd>
);

}
