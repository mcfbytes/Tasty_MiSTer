// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/deadzone_rule.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "infra/ascii.h"
#include "svc/device_facts.h"
#include "svc/vid_pid_key.h"

namespace mister::svc {

namespace {

std::optional<std::pair<UidKind, std::string_view>> vid_pid_digits(std::string_view t) {
    if (t.empty())
        return std::nullopt;
    const char head = infra::ascii_lower(t[0]);
    if (head != 'v' && head != 'p')
        return std::nullopt;
    std::size_t i = 1;
    std::size_t n = 0;
    while (i < t.size() && infra::ascii_lower(t[i]) == 'i') {
        ++i;
        ++n;
    }
    if (n == 0)
        return std::nullopt;
    n = 0;
    while (i < t.size() && infra::ascii_lower(t[i]) == 'd') {
        ++i;
        ++n;
    }
    if (n == 0 || i >= t.size() || t[i] != ':')
        return std::nullopt;
    return std::pair<UidKind, std::string_view>{
        head == 'v' ? UidKind::Vid : UidKind::Pid,
        t.substr(i + 1)
    };
}

}

std::optional<DeadzoneMatcher> parse_deadzone_matcher(std::string_view token) {
    DeadzoneMatcher m;
    if (!m.uid.assign(token))
        return std::nullopt;

    if (const auto key = VidPidKey::parse_hex(token)) {
        m.kind = UidKind::VidPid;
        m.key = *key;
    } else if (const auto half = vid_pid_digits(token)) {
        if (half->first == UidKind::Vid) {
            if (const auto vid = Vid::parse_hex(half->second)) {
                m.kind = UidKind::Vid;
                m.vid = *vid;
            }
        } else if (const auto pid = Pid::parse_hex(half->second)) {
            m.kind = UidKind::Pid;
            m.pid = *pid;
        }
    }
    return m;
}

bool DeadzoneMatcher::matches(const DeviceFacts& dev) const {
    switch (kind) {
        case UidKind::VidPid:
            return VidPidKey::of(dev.id.vid, dev.id.pid) == key;
        case UidKind::Vid:
            return dev.id.vid == vid;
        case UidKind::Pid:
            return dev.id.pid == pid;
        case UidKind::Name:

            return infra::icontains(dev.id.merge_id, uid.view())
                || infra::icontains(dev.sysfs, uid.view())
                || infra::icontains(dev.id.id, uid.view());
    }
    return false;
}

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
    const auto match = parse_deadzone_matcher(line.substr(0, i));
    if (!match)
        return std::nullopt;
    DeadzoneRule r{};
    r.match = *match;

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
