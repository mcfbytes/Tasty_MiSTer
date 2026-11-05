// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>

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

struct ItemId {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr proto::SubmenuNo submenu() const noexcept {
        constexpr unsigned kLowByte = 0xFFu;
        return proto::SubmenuNo{static_cast<std::uint8_t>(v & kLowByte)};
    }

    friend constexpr bool operator==(ItemId, ItemId) = default;
};

inline constexpr ItemId kNoItem{0};

struct RowCount {
    std::uint32_t v = 0;
    using affine_count = RowCount;

    template <std::integral I>
    [[nodiscard]] static constexpr RowCount of(I n) noexcept {
        return RowCount{static_cast<std::uint32_t>(n)};
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return v; }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr RowCount half() const noexcept { return RowCount{v >> 1u}; }

    template <class Rows>
    constexpr void clip(Rows& rows) const {
        if (rows.size() > v)
            rows.resize(v);
    }

    friend constexpr bool operator==(RowCount, RowCount) = default;
    friend constexpr auto operator<=>(RowCount, RowCount) = default;
};

struct RowIndex {
    std::uint32_t v = 0;
    using affine_count = RowCount;

    template <std::integral I>
    [[nodiscard]] static constexpr RowIndex of(I n) noexcept {
        return RowIndex{static_cast<std::uint32_t>(n)};
    }

    [[nodiscard]] constexpr RowCount before() const noexcept { return RowCount{v}; }

    friend constexpr bool operator==(RowIndex, RowIndex) = default;
    friend constexpr auto operator<=>(RowIndex, RowIndex) = default;
};

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

[[nodiscard]] constexpr RowIndex row_after(RowCount count) noexcept {
    return RowIndex{count.v};
}

[[nodiscard]] constexpr RowIndex last_row(RowCount count) noexcept {
    return row_after(count - kOneRow);
}

[[nodiscard]] constexpr RowCount saturating_sub(RowCount count, RowCount height) noexcept {
    return RowCount{count.v > height.v ? count.v - height.v : 0u};
}

struct RowKey {
    std::uint32_t v = 0;
    friend constexpr bool operator==(RowKey, RowKey) = default;
};

[[nodiscard]] constexpr proto::OsdRowCount osd_rows_of(RowCount height) noexcept {
    return proto::OsdRowCount::of(height.v);
}

[[nodiscard]] constexpr proto::OsdRow last_osd_row(RowCount height) noexcept {
    return proto::OsdRow::of(height.v - 1u);
}

[[nodiscard]] constexpr proto::OsdRow osd_row_at(RowCount offset) noexcept {
    return proto::OsdRow::of(offset.v);
}

[[nodiscard]] constexpr RowIndex row_index_of(proto::OsdRow row) noexcept {
    return RowIndex{row.v};
}

[[nodiscard]] constexpr proto::DipRowIndex dip_row_of(RowIndex row) noexcept {
    return proto::DipRowIndex::of(row.v);
}

struct MenuMask {
    std::uint64_t v = 0;
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

struct HdMask {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr proto::OsdMask osd_word() const noexcept {
        return proto::OsdMask{static_cast<std::uint16_t>(v)};
    }

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

}
