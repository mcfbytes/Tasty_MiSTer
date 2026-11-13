// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "infra/fixed_str.h"
#include "svc/config_snapshot.h"
#include "svc/types.h"
#include "svc/vid_pid_key.h"

namespace mister::svc {

struct DeviceFacts;

inline constexpr std::size_t kDeadzoneUidChars = 255;

enum class UidKind : std::uint8_t {
    VidPid,
    Vid,
    Pid,
    Name,
};

struct DeadzoneMatcher {
    FixedStr<kDeadzoneUidChars + 1, StrFit::Reject> uid{};
    UidKind kind = UidKind::Name;
    VidPidKey key{};
    Vid vid{};
    Pid pid{};

    [[nodiscard]] bool matches(const DeviceFacts& dev) const;
};

[[nodiscard]] std::optional<DeadzoneMatcher> parse_deadzone_matcher(std::string_view token);

struct DeadzoneRule {

    DeadzoneMatcher match{};
    Deadzone deadzone{};
};

[[nodiscard]] std::optional<DeadzoneRule> parse_deadzone_rule(std::string_view line);

[[nodiscard]] std::vector<DeadzoneRule> parse_deadzone_rules(const ConfigSnapshot& cfg);

}
