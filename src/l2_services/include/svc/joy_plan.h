// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

#include "infra/seat.h"
#include "svc/joy_bit.h"
#include "svc/sys_button.h"

namespace mister::svc {

struct JoyPlan {
    static constexpr std::int8_t kNone = -1;
    static constexpr std::size_t kWords = 32;
    static constexpr std::int8_t kFirstButtonWord = 4;
    static constexpr std::int8_t kLastButtonWord = 11;
    static_assert(
        kLastButtonWord - kFirstButtonWord == 7,
        "item: the SYS_BTN_ button-word span is eight words wide"
    );

    std::array<std::optional<SysButton>, kWords> src{};

    std::optional<JoyBit> paddle_idx{};
    bool present = false;

    bool stick_dpad = true;
};

static_assert(std::is_trivially_copyable_v<JoyPlan>, "JoyPlan is copied between threads by value");

struct ButtonLists {
    std::string_view j;
    std::string_view jn;
    std::string_view jp;

    [[nodiscard]] constexpr ButtonLists with_override(
        std::string_view names,
        std::string_view defaults
    ) const noexcept {
        return {names.empty() ? j : names, defaults.empty() ? jn : defaults, jp};
    }
};

JoyPlan build_joy_plan(const ButtonLists& lists, bool gamepad_defaults) noexcept;

}
