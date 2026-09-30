// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "infra/fixed_str.h"
#include "svc/config_snapshot.h"

namespace mister::svc {

inline constexpr std::size_t kDeadzoneUidChars = 255;

struct DeadzoneRule {

    FixedStr<kDeadzoneUidChars + 1, StrFit::Reject> uid{};
    std::uint8_t deadzone = 0;
};

[[nodiscard]] std::optional<DeadzoneRule> parse_deadzone_rule(std::string_view line);

[[nodiscard]] std::vector<DeadzoneRule> parse_deadzone_rules(const ConfigSnapshot& cfg);

}  // namespace mister::svc
