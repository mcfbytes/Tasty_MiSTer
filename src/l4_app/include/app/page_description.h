// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "infra/fixed_str.h"

namespace mister::app {

struct PageDescription {
    static constexpr std::size_t kMaxRows = 12;
    enum class RowKind : std::uint8_t { Action, Submenu, Value };
    struct Row {
        FixedStr<40, StrFit::Clip> label{};
        FixedStr<24, StrFit::Clip> value{};
        RowKind kind = RowKind::Action;
        bool enabled = true;
        bool selected = false;
        friend bool operator==(const Row&, const Row&) = default;
    };
    FixedStr<48, StrFit::Clip> title{};
    FixedStr<48, StrFit::Clip> crumb{};
    std::array<Row, kMaxRows> rows{};
    std::uint8_t row_count = 0;
    std::uint8_t first_visible = 0;
    friend bool operator==(const PageDescription&, const PageDescription&) = default;
};

}  // namespace mister::app
