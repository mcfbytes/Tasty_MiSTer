// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <variant>

#include "cores/send_bracket.h"
#include "cores/stage_window.h"

namespace mister::cores {

enum class ActKind : std::uint8_t { StageWindow, SendBracket, kCount };
inline constexpr std::size_t kActs = static_cast<std::size_t>(ActKind::kCount);

using ManifestAct = std::variant<StageWindow, SendBracket>;

static_assert(std::variant_size_v<ManifestAct> == kActs, "ManifestAct: one name per arm");
template <ActKind K, class T>
inline constexpr bool kArmIs = std::
    is_same_v<std::variant_alternative_t<static_cast<std::size_t>(K), ManifestAct>, T>;
static_assert(
    kArmIs<ActKind::StageWindow, StageWindow> && kArmIs<ActKind::SendBracket, SendBracket>
);

}
