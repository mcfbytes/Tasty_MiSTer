// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "ui/key_grid_layer.h"
#include "ui/text_entry_kind.h"
#include "ui/types.h"

namespace mister::ui {

class KeyGrid {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::size_t kRows = 4;
    static constexpr std::size_t kWidest = 10;
    using Line = FixedStr<24, StrFit::Clip>;

    explicit KeyGrid(TextEntryKind kind) noexcept
        : layer_(text_entry_numeric(kind) ? KeyGridLayer::Digits : KeyGridLayer::Lower) {}

    [[nodiscard]] KeyGridLayer layer() const noexcept { return layer_; }

    void toggle_layer() noexcept;

    [[nodiscard]] std::optional<char> key_at(RowIndex row) const noexcept;

    void step(RowIndex row, CycleDir dir) noexcept;

    [[nodiscard]] Line line(RowIndex row, bool with_cursor) const noexcept;

private:
    [[nodiscard]] std::string_view keys_(RowIndex row) const noexcept;
    [[nodiscard]] std::size_t column_on_(RowIndex row) const noexcept;

    KeyGridLayer layer_;
    std::size_t col_ = 0;
};

}
