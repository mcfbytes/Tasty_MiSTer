// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "infra/fixed_str.h"

namespace mister::svc {

inline constexpr std::size_t kYcMaxEntries = 20;

inline constexpr std::size_t kYcKeyChars = 63;

struct YcMode {
    FixedStr<kYcKeyChars + 1, StrFit::Clip> key{};
    std::int64_t phase_inc = 0;
};

std::vector<YcMode> parse_yc(std::string_view text, std::string_view core_name);

std::optional<std::int64_t> yc_lookup(
    std::span<const YcMode> table,
    std::string_view key,
    std::string_view key_expanded
);

}
