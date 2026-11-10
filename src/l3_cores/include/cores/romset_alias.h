// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>

#include "cores/romset_drop.h"
#include "cores/romset_no_match.h"
#include "cores/romset_retype.h"

#include "infra/opt_ref.h"

namespace mister::cores {

using RomsetAlias = std::variant<RomsetNoMatch, RomsetRetype, RomsetDrop>;

enum class RomsetAliasKind : std::uint8_t { NoMatch, Retype, Drop, kCount };
inline constexpr std::size_t kRomsetAliasArms = static_cast<std::size_t>(RomsetAliasKind::kCount);
static_assert(
    std::variant_size_v<RomsetAlias> == kRomsetAliasArms,
    "RomsetAlias: one name per arm"
);
template <RomsetAliasKind K, class T>
inline constexpr bool kRomsetArmIs = std::
    is_same_v<std::variant_alternative_t<static_cast<std::size_t>(K), RomsetAlias>, T>;
static_assert(
    kRomsetArmIs<RomsetAliasKind::NoMatch, RomsetNoMatch>
    && kRomsetArmIs<RomsetAliasKind::Retype, RomsetRetype>
    && kRomsetArmIs<RomsetAliasKind::Drop, RomsetDrop>
);

[[nodiscard]] inline infra::OptRef<const std::string> alias_of(const RomsetAlias& a) noexcept {
    if (const auto* retype = std::get_if<RomsetRetype>(&a))
        return infra::OptRef<const std::string>{retype->alias};
    return {};
}

[[nodiscard]] inline bool drops(const RomsetAlias& a) noexcept {
    return std::holds_alternative<RomsetDrop>(a);
}

}
