// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "proto/types.h"

namespace mister::cores {

struct OptionRowView {

    std::uint8_t current = 0;
    bool dimmed = false;
    std::string_view text{};

    [[nodiscard]] static constexpr OptionRowView cycle(
        proto::FieldValue current,
        std::string_view text
    ) noexcept {
        return OptionRowView{static_cast<std::uint8_t>(current.v), false, text};
    }
};

}
