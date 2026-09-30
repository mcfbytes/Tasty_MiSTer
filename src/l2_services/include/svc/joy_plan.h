// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include "infra/seat.h"

namespace mister::svc {

struct JoyPlan {
    TASTY_SEAT_EXEMPT(component);
    static constexpr std::int8_t kNone = -1;
    static constexpr std::size_t kWords = 32;
    static constexpr std::int8_t kFirstButtonWord = 4;
    static constexpr std::int8_t kLastButtonWord = 11;
    static_assert(kLastButtonWord - kFirstButtonWord == 7,
                  "item: the SYS_BTN_ button-word span is eight words wide");
    static constexpr std::array<std::int8_t, kWords> all_none() noexcept {
        std::array<std::int8_t, kWords> a{};
        for (std::int8_t& v : a)
            v = kNone;
        return a;
    }
    std::array<std::int8_t, kWords> src = all_none();
    std::int8_t paddle_idx = kNone;
    bool present = false;
};

JoyPlan build_joy_plan(std::string_view j_list, std::string_view jn_list, std::string_view jp_list,
                       bool gamepad_defaults) noexcept;

struct ButtonLists {
    std::string_view j;
    std::string_view jn;
    std::string_view jp;
};

[[nodiscard]] constexpr ButtonLists choose_button_lists(std::string_view ovr_names,
                                                        std::string_view ovr_defaults,
                                                        std::string_view j, std::string_view jn,
                                                        std::string_view jp) noexcept {
    return {ovr_names.empty() ? j : ovr_names, ovr_defaults.empty() ? jn : ovr_defaults, jp};
}

}  // namespace mister::svc
