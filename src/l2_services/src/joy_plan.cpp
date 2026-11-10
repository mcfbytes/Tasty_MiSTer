// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/joy_plan.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "infra/ascii.h"
#include "infra/fixed_str.h"
#include "svc/sys_button.h"

namespace mister::svc {

namespace {

constexpr std::size_t kMaxJoyButtons = 28;

constexpr std::size_t kNameCap = 32;

using NameBuf = FixedStr<kNameCap, StrFit::Clip>;

std::string_view substr_field(std::string_view s, std::size_t idx) noexcept {
    std::size_t field = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != ',')
            continue;
        if (field == idx)
            return s.substr(start, i - start);
        ++field;
        start = i + 1;
    }
    return field == idx ? s.substr(start) : std::string_view{};
}

std::string_view clip_capacity(std::string_view s) noexcept {
    return s.substr(0, NameBuf::kCapacity);
}

std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && s.back() == ' ')
        s.remove_suffix(1);
    while (!s.empty() && s.front() == ' ')
        s.remove_prefix(1);
    return s;
}

std::string_view before(std::string_view s, char cut) noexcept {
    const std::size_t pos = s.find(cut);
    return pos == std::string_view::npos ? s : s.substr(0, pos);
}

bool has_char(std::string_view s, char c) noexcept {
    return s.find(c) != std::string_view::npos;
}

int is_fire(std::string_view name) noexcept {
    if (!infra::istarts_with(name, "fire") && !infra::istarts_with(name, "button"))
        return 0;
    if (infra::ieq(name, "fire") || has_char(name, '1'))
        return 1;
    if (has_char(name, '2'))
        return 2;
    if (has_char(name, '3'))
        return 3;
    if (has_char(name, '4'))
        return 4;
    return 0;
}

std::optional<SysButton> sys_for_button_name(std::string_view n) noexcept {
    if (infra::ieq(n, "A") || infra::ieq(n, "Jump") || is_fire(n) == 1)
        return kSysBtnA;
    if (infra::ieq(n, "B") || is_fire(n) == 2)
        return kSysBtnB;
    if (infra::ieq(n, "X") || infra::ieq(n, "C") || is_fire(n) == 3)
        return kSysBtnX;
    if (infra::ieq(n, "Y") || infra::ieq(n, "D") || is_fire(n) == 4)
        return kSysBtnY;
    if (infra::ieq(n, "R") || infra::ieq(n, "RT") || infra::ieq(n, "Coin"))
        return kSysBtnR;
    if (infra::ieq(n, "L") || infra::ieq(n, "LT"))
        return kSysBtnL;
    if (infra::ieq(n, "Select")
        || infra::ieq(n, "Mode")
        || infra::ieq(n, "Game Select")
        || infra::ieq(n, "Start 2P"))
        return kSysBtnSelect;
    if (infra::ieq(n, "Start")
        || infra::ieq(n, "Run")
        || infra::ieq(n, "Pause")
        || infra::ieq(n, "Start 1P"))
        return kSysBtnStart;
    return std::nullopt;
}

}

JoyPlan build_joy_plan(
    std::string_view j_list,
    std::string_view jn_list,
    std::string_view jp_list,
    bool gamepad_defaults
) noexcept {
    JoyPlan plan{};

    std::array<NameBuf, kMaxJoyButtons> raw{};
    std::array<NameBuf, kMaxJoyButtons> nnames{};
    std::array<NameBuf, kMaxJoyButtons> pnames{};
    std::size_t joy_count = 0;
    if (!j_list.empty()) {
        for (std::size_t n = 0; n < kMaxJoyButtons; ++n) {
            const std::string_view field = clip_capacity(substr_field(j_list, n));
            (void)raw[n].assign(field);
            if (raw[n].empty())
                break;
            (void)nnames[n].assign(trim(before(field, '(')));
            if (nnames[n].empty())
                break;
            ++joy_count;
        }
    }

    if (!jn_list.empty()) {
        for (NameBuf& b : nnames)
            b.clear();
        for (std::size_t n = 0; n < joy_count; ++n) {
            (void)nnames[n].assign(trim(clip_capacity(substr_field(jn_list, n))));
        }
    }

    bool defaults = false;
    if (!jp_list.empty()) {
        defaults = gamepad_defaults;
        for (std::size_t n = 0; n < joy_count; ++n) {
            (void)pnames[n].assign(trim(clip_capacity(substr_field(jp_list, n))));
        }
    }

    for (std::size_t i = 0, n = 0; i < joy_count; ++i) {
        if (raw[i].view() == "-")
            continue;

        const NameBuf& full = defaults ? pnames[n] : nnames[n];
        const std::string_view name = full.view();
        const std::optional<SysButton> sys = sys_for_button_name(before(name, '|'));
        const std::size_t idx = i + JoyPlan::kFirstButtonWord;
        if (sys && is_button(*sys) && idx < JoyPlan::kWords)
            plan.src[idx] = sys;

        const std::size_t bar = name.find('|');
        if (plan.paddle_idx == JoyPlan::kNone
            && bar != std::string_view::npos
            && infra::ieq(name.substr(bar), "|P")) {
            plan.paddle_idx = static_cast<std::int8_t>(idx);
        }
        ++n;
    }

    plan.present = (joy_count != 0);
    return plan;
}

}
