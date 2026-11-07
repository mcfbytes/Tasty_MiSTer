// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <bit>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "app/types.h"
#include "infra/affine.h"
#include "infra/indexed_span.h"
#include "proto/types.h"

namespace mister::ui {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator*;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

enum class KeyEdge : std::uint8_t { Make, Break };

enum class CycleDir : std::int8_t { Next, Back };

struct ItemId {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr proto::SubmenuNo submenu() const noexcept {
        constexpr unsigned kLowByte = 0xFFu;
        return proto::SubmenuNo{static_cast<std::uint8_t>(v & kLowByte)};
    }

    friend constexpr bool operator==(ItemId, ItemId) = default;
};

inline constexpr ItemId kNoItem{0};

using app::config_slot_of;
using app::dip_row_of;
using app::last_osd_row;
using app::osd_row_at;
using app::osd_rows_of;
using app::row_after;
using app::row_index_of;
using app::RowCount;
using app::RowIndex;
using app::saturating_sub;

[[nodiscard]] constexpr ItemId item_submenu(proto::SubmenuNo n) noexcept {
    return ItemId{n.v};
}

[[nodiscard]] constexpr ItemId item_row(RowIndex r) noexcept {
    return ItemId{static_cast<std::uint16_t>(r.v)};
}

template <class T>
using RowSpan = infra::IndexedSpan<T, RowIndex>;

class RowRange {
public:
    class Iter {
    public:
        constexpr explicit Iter(RowIndex at) noexcept : at_(at) {}

        [[nodiscard]] constexpr RowIndex operator*() const noexcept { return at_; }

        constexpr Iter& operator++() noexcept {
            at_ += RowCount{1};
            return *this;
        }

        friend constexpr bool operator==(Iter, Iter) = default;

    private:
        RowIndex at_;
    };

    constexpr RowRange(RowIndex first, RowCount count) noexcept : first_(first), count_(count) {}

    [[nodiscard]] constexpr Iter begin() const noexcept { return Iter{first_}; }

    [[nodiscard]] constexpr Iter end() const noexcept { return Iter{first_ + count_}; }

    [[nodiscard]] constexpr bool contains(RowIndex r) const noexcept {
        return r >= first_ && r - first_ < count_;
    }

private:
    RowIndex first_;
    RowCount count_;
};

[[nodiscard]] constexpr RowRange rows(RowCount count) noexcept {
    return RowRange{RowIndex{}, count};
}

[[nodiscard]] constexpr RowRange rows(RowIndex first, RowCount count) noexcept {
    return RowRange{first, count};
}

inline constexpr RowCount kOneRow{1};

inline constexpr RowCount kFullOsdRows{16};

[[nodiscard]] constexpr RowIndex last_row(RowCount count) noexcept {
    return row_after(count - kOneRow);
}

enum class RowKeySpace : std::uint32_t { Push = 0x2, Slot = 0x4 };

struct RowKey {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr RowKey in_space(RowKeySpace space, ItemId ordinal) noexcept {
        constexpr unsigned kSpaceShift = 16;
        return RowKey{(static_cast<std::uint32_t>(space) << kSpaceShift) | ordinal.v};
    }

    friend constexpr bool operator==(RowKey, RowKey) = default;
};

struct MenuMask {
    std::uint64_t v = 0;

    [[nodiscard]] constexpr bool has(RowIndex r) const noexcept;

    [[nodiscard]] constexpr MenuMask with(RowIndex r) const noexcept;

    [[nodiscard]] constexpr MenuMask only(MenuMask subset) const noexcept;

    [[nodiscard]] constexpr MenuMask without_row(RowIndex r) const noexcept;

    [[nodiscard]] constexpr MenuMask without(MenuMask drop) const noexcept {
        return MenuMask{v & ~drop.v};
    }

    [[nodiscard]] constexpr std::optional<RowIndex> lowest() const noexcept;

    [[nodiscard]] constexpr std::optional<RowIndex> highest() const noexcept;

    [[nodiscard]] constexpr std::optional<RowIndex> next_after(RowIndex r) const noexcept;

    [[nodiscard]] constexpr std::optional<RowIndex> prev_before(RowIndex r) const noexcept;

    friend constexpr bool operator==(MenuMask, MenuMask) = default;
};

[[nodiscard]] constexpr RowCount menu_mask_rows() noexcept {
    constexpr std::uint32_t kBits = 64;
    return RowCount{kBits};
}

[[nodiscard]] constexpr MenuMask dense_mask(RowCount n) noexcept {
    if (n >= menu_mask_rows())
        return MenuMask{~std::uint64_t{0}};
    return MenuMask{(std::uint64_t{1} << n.v) - 1u};
}

[[nodiscard]] constexpr MenuMask row_bit(RowIndex r) noexcept {
    return r.before() < menu_mask_rows() ? MenuMask{std::uint64_t{1} << r.v} : MenuMask{};
}

[[nodiscard]] constexpr MenuMask with_row(MenuMask m, RowIndex r) noexcept {
    return MenuMask{m.v | row_bit(r).v};
}

[[nodiscard]] constexpr bool has_row(MenuMask m, RowIndex r) noexcept {
    return (m.v & row_bit(r).v) != 0;
}

constexpr bool MenuMask::has(RowIndex r) const noexcept {
    return has_row(*this, r);
}

constexpr MenuMask MenuMask::with(RowIndex r) const noexcept {
    return with_row(*this, r);
}

constexpr MenuMask MenuMask::only(MenuMask subset) const noexcept {
    return MenuMask{v & subset.v};
}

constexpr MenuMask MenuMask::without_row(RowIndex r) const noexcept {
    return MenuMask{v & ~row_bit(r).v};
}

constexpr std::optional<RowIndex> MenuMask::lowest() const noexcept {
    if (v == 0)
        return std::nullopt;
    return RowIndex{static_cast<std::uint32_t>(std::countr_zero(v))};
}

constexpr std::optional<RowIndex> MenuMask::highest() const noexcept {
    constexpr std::uint32_t kTop = menu_mask_rows().v - 1u;
    if (v == 0)
        return std::nullopt;
    return RowIndex{kTop - static_cast<std::uint32_t>(std::countl_zero(v))};
}

constexpr std::optional<RowIndex> MenuMask::next_after(RowIndex r) const noexcept {
    if (r >= row_after(menu_mask_rows() - kOneRow))
        return std::nullopt;
    return without(dense_mask(r.before() + kOneRow)).lowest();
}

constexpr std::optional<RowIndex> MenuMask::prev_before(RowIndex r) const noexcept {
    return only(dense_mask(r.before())).highest();
}

struct HdMask {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr proto::OsdMask osd_word() const noexcept {
        return proto::OsdMask{static_cast<std::uint16_t>(v)};
    }

    [[nodiscard]] constexpr bool has(HdMask bits) const noexcept { return (v & bits.v) != 0u; }

    friend constexpr bool operator==(HdMask, HdMask) = default;
};

struct UiButtonMask {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr bool has_any(UiButtonMask bits) const noexcept {
        return (v & bits.v) != 0;
    }

    [[nodiscard]] constexpr UiButtonMask with(UiButtonMask bits) const noexcept {
        return UiButtonMask{static_cast<std::uint16_t>(v | bits.v)};
    }

    [[nodiscard]] constexpr UiButtonMask without(UiButtonMask bits) const noexcept {
        return UiButtonMask{static_cast<std::uint16_t>(v & ~bits.v)};
    }

    friend constexpr bool operator==(UiButtonMask, UiButtonMask) = default;
};

static_assert(last_row(RowCount{3}) == RowIndex{2});
static_assert(rows(RowCount{3}).contains(RowIndex{2}) && !rows(RowCount{3}).contains(RowIndex{3}));
static_assert(has_row(MenuMask{0b100u}, RowIndex{2}) && !has_row(MenuMask{0b100u}, RowIndex{64}));
static_assert(row_after(RowCount{3}) - RowIndex{} == RowCount{3});
static_assert(dense_mask(RowCount{3}) == MenuMask{0b111u});
static_assert(dense_mask(menu_mask_rows()) == MenuMask{~std::uint64_t{0}});
static_assert(MenuMask{}.lowest() == std::nullopt && MenuMask{}.highest() == std::nullopt);
static_assert(MenuMask{0b1010u}.lowest() == RowIndex{1});
static_assert(MenuMask{0b1010u}.highest() == RowIndex{3});
static_assert(MenuMask{0b1010u}.next_after(RowIndex{1}) == RowIndex{3});
static_assert(MenuMask{0b1010u}.next_after(RowIndex{3}) == std::nullopt);
static_assert(MenuMask{0b1010u}.prev_before(RowIndex{3}) == RowIndex{1});
static_assert(MenuMask{0b1010u}.prev_before(RowIndex{0}) == std::nullopt);
static_assert(MenuMask{0b1010u}.prev_before(RowIndex{40}) == RowIndex{3});
static_assert(MenuMask{0b1111u}.only(MenuMask{0b0101u}) == MenuMask{0b0101u});
static_assert(MenuMask{0b1111u}.without_row(RowIndex{1}) == MenuMask{0b1101u});
static_assert(MenuMask{}.with(RowIndex{2}) == MenuMask{0b100u});
static_assert(!MenuMask{}.has(RowIndex{2}) && MenuMask{0b100u}.has(RowIndex{2}));
static_assert(sizeof(MenuMask) == 8);
static_assert(sizeof(CycleDir) == 1);

}
