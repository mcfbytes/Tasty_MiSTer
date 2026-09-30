// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "cores/option_row.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct OptionTable {
    TASTY_SEAT_EXEMPT(const_shared);
    struct Row {
        const cores::OptionRow* decl = nullptr;
        std::uint8_t current = 0;
        bool dimmed = false;
        FixedStr<cores::kOptionTextChars + 1, StrFit::Clip> text{};

        FixedStr<16, StrFit::Clip> ext{};
    };
    std::uint8_t count = 0;
    Row rows[cores::kMaxOptionRows] = {};

    FixedStr<cores::kOptionTitleChars + 1, StrFit::Clip> page_title[cores::kMaxOptionPages] = {};
};
static_assert(std::is_trivially_copyable_v<OptionTable>);

using OptionCell = xthread::Telemetry<OptionTable, SeatTag::RT>;

}  // namespace mister::app
