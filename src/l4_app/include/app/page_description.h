// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "infra/fixed_str.h"

namespace mister::app {

enum class RowTier : std::uint8_t { Place, Verb, Shelf };

[[nodiscard]] constexpr std::uint8_t tier_bit(RowTier t) noexcept {
    return static_cast<std::uint8_t>(1u << static_cast<unsigned>(t));
}

struct PageDescription {
    static constexpr std::size_t kMaxRows = 12;
    enum class RowKind : std::uint8_t { Action, Submenu, Value, Separator, Text, Bar };
    enum class Shape : std::uint8_t { List, Choice };

    enum class RowIcon : std::uint8_t {
        None,
        Folder,
        Archive,
        Up,
        Recent,
        Cores,
        Bluetooth,
        Settings
    };

    struct Row {
        FixedStr<128, StrFit::Clip> label{};
        FixedStr<24, StrFit::Clip> value{};
        RowKind kind = RowKind::Action;
        RowIcon icon = RowIcon::None;
        RowTier tier = RowTier::Place;
        bool enabled = true;
        bool selected = false;
        bool steps = false;
        std::uint8_t fill = 0;
        friend bool operator==(const Row&, const Row&) = default;
    };

    FixedStr<48, StrFit::Clip> title{};
    FixedStr<48, StrFit::Clip> crumb{};

    FixedStr<64, StrFit::Clip> subtitle{};
    std::array<Row, kMaxRows> rows{};
    std::uint8_t row_count = 0;
    std::uint16_t first = 0;
    std::uint16_t total = 0;
    std::uint8_t pinned = 0;
    Shape shape = Shape::List;

    char letter = '\0';
    FixedStr<48, StrFit::Clip> footer{};

    struct Hub {
        struct Item {
            FixedStr<32, StrFit::Clip> label{};
            RowIcon icon = RowIcon::None;
            bool enabled = true;
            friend bool operator==(const Item&, const Item&) = default;
        };

        std::array<Item, kMaxRows> items{};
        std::uint8_t count = 0;

        std::uint8_t open = 0;

        std::uint8_t depth = 0;
        friend bool operator==(const Hub&, const Hub&) = default;
    };

    Hub hub{};
    friend bool operator==(const PageDescription&, const PageDescription&) = default;
};

static_assert(std::is_trivially_copyable_v<PageDescription>);

}
