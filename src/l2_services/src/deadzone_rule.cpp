// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/deadzone_rule.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace mister::svc {

std::optional<DeadzoneRule> parse_deadzone_rule(std::string_view line) {

    const auto is_sep = [](char c) {
        return c == ' ' || c == '\t' || c == ',';
    };
    std::size_t i = 0;
    while (i < line.size() && !is_sep(line[i]))
        ++i;
    if (i == 0 || i > kDeadzoneUidChars)
        return std::nullopt;
    std::size_t j = i;
    while (j < line.size() && is_sep(line[j]))
        ++j;
    if (j == i)
        return std::nullopt;
    const std::size_t digits = j;
    std::uint32_t value = 0;
    while (j < line.size() && line[j] >= '0' && line[j] <= '9') {
        value = value < 0x1000u ? value * 10u + static_cast<std::uint32_t>(line[j] - '0') : value;
        ++j;
    }
    if (j == digits || j != line.size())
        return std::nullopt;
    DeadzoneRule r{};
    if (!r.uid.assign(line.substr(0, i)))
        return std::nullopt;

    r.deadzone = Deadzone::clamped(value);
    return r;
}

std::vector<DeadzoneRule> parse_deadzone_rules(const ConfigSnapshot& cfg) {
    std::vector<DeadzoneRule> out;
    for (const auto& row : cfg.controller_deadzone) {
        const std::string_view line{static_cast<const char*>(row)};
        if (line.empty())
            break;
        if (auto r = parse_deadzone_rule(line))
            out.push_back(*r);
    }
    return out;
}

}
