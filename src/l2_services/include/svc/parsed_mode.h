// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <variant>

#include "svc/modeline.h"
#include "svc/parsed_kind.h"
#include "svc/pll_block.h"
#include "svc/preset_pick.h"

namespace mister::svc::video_timing {

struct CustomMode {
    Modeline mode{};
    std::optional<PllBlock> raw_pll{};
};

using ParsedMode = std::variant<PresetPick, CustomMode>;

static_assert(
    std::variant_size_v<ParsedMode> == arm_index(ParsedKind::kCount),
    "ParsedMode: one ParsedKind name per arm"
);
template <ParsedKind K, class T>
inline constexpr bool
    kArmIs = std::is_same_v<std::variant_alternative_t<arm_index(K), ParsedMode>, T>;
static_assert(
    kArmIs<ParsedKind::PresetPick, PresetPick> && kArmIs<ParsedKind::CustomMode, CustomMode>
);

}
