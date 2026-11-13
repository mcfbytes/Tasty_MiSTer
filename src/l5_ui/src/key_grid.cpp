// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/key_grid.h"

#include <algorithm>
#include <array>

#include "infra/message_sum.h"
#include "infra/opt_ref.h"

namespace mister::ui {
namespace {

using Rows = std::array<std::string_view, KeyGrid::kRows>;

constexpr Rows kLower{"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm._@"};
constexpr Rows kUpper{"1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM._@"};
constexpr Rows kSymbols{"!@#$%^&*()", "-_=+[]{}\\|", ";:'\",.<>/?", "`~"};
constexpr Rows kDigits{"123", "456", "789", "0"};

constexpr const Rows& rows_of(KeyGridLayer layer) noexcept {
    switch (layer) {
        case KeyGridLayer::Lower:
            return kLower;
        case KeyGridLayer::Upper:
            return kUpper;
        case KeyGridLayer::Symbols:
            return kSymbols;
        case KeyGridLayer::Digits:
            return kDigits;
    }
    return kLower;
}

constexpr char kOpen = '[';
constexpr char kClose = ']';
constexpr char kGap = ' ';

}

void KeyGrid::toggle_layer() noexcept {
    TASTY_SEAT_BODY(KeyGrid);
    layer_ = next_layer(layer_);
}

std::string_view KeyGrid::keys_(RowIndex row) const noexcept {
    const infra::OptRef<const std::string_view>
        keys = RowSpan<const std::string_view>{rows_of(layer_)}.find(row);
    return keys ? *keys : std::string_view{};
}

std::size_t KeyGrid::column_on_(RowIndex row) const noexcept {
    const std::size_t width = keys_(row).size();
    return width == 0 ? 0 : std::min(col_, width - 1);
}

std::optional<char> KeyGrid::key_at(RowIndex row) const noexcept {
    TASTY_SEAT_BODY(KeyGrid);
    const std::string_view keys = keys_(row);
    if (keys.empty())
        return std::nullopt;
    return keys[column_on_(row)];
}

void KeyGrid::step(RowIndex row, CycleDir dir) noexcept {
    TASTY_SEAT_BODY(KeyGrid);
    const std::size_t width = keys_(row).size();
    if (width == 0)
        return;
    const std::size_t at = column_on_(row);
    col_ = dir == CycleDir::Next ? (at + 1) % width : (at + width - 1) % width;
}

KeyGrid::Line KeyGrid::line(RowIndex row, bool with_cursor) const noexcept {
    TASTY_SEAT_BODY(KeyGrid);
    Line out{};
    const std::string_view keys = keys_(row);
    const std::size_t cursor = column_on_(row);
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (i != 0)
            (void)out.append(std::string_view{&kGap, 1});
        const bool at = with_cursor && i == cursor;
        if (at)
            (void)out.append(std::string_view{&kOpen, 1});
        (void)out.append(keys.substr(i, 1));
        if (at)
            (void)out.append(std::string_view{&kClose, 1});
    }
    return out;
}

}
