// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

#include "cores/option_row.h"
#include "infra/fixed_str.h"
#include "infra/indexed_array.h"
#include "proto/types.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "infra/opt_ref.h"

namespace mister::app {

struct OptionTable {
    TASTY_SEAT_EXEMPT(const_shared);

    struct Row {
        infra::OptRef<const cores::OptionRow> decl{};
        std::uint8_t current = 0;
        bool dimmed = false;
        FixedStr<cores::kOptionTextChars + 1, StrFit::Clip> text{};

        FixedStr<16, StrFit::Clip> ext{};
    };

    std::uint8_t count = 0;
    Row rows[cores::kMaxOptionRows] = {};

    infra::IndexedArray<
        FixedStr<cores::kOptionTitleChars + 1, StrFit::Clip>,
        cores::kMaxOptionPages,
        proto::SubmenuNo>
        page_title{};

    [[nodiscard]] constexpr std::optional<std::string_view> title(
        proto::SubmenuNo page
    ) const noexcept {
        const auto declared = page_title.find(page);
        if (!declared || declared->view().empty())
            return std::nullopt;
        return declared->view();
    }
};

static_assert(std::is_trivially_copyable_v<OptionTable>);

using OptionCell = xthread::Telemetry<OptionTable, SeatTag::RT>;

}
