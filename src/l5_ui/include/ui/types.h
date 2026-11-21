// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <bit>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

#include "app/types.h"
#include "infra/affine.h"
#include "infra/index_set.h"
#include "infra/indexed_span.h"
#include "infra/run.h"
#include "proto/types.h"

namespace mister::ui {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator*;
using ::mister::infra::affine::operator/;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

enum class KeyEdge : std::uint8_t { Make, Break };

using app::CycleDir;

struct StackDepth {
    std::size_t v = 0;

    template <std::integral I>
    [[nodiscard]] static constexpr StackDepth truncating(I n) noexcept {
        return StackDepth{static_cast<std::size_t>(n)};
    }

    [[nodiscard]] constexpr bool above_bottom() const noexcept { return v != 0; }

    [[nodiscard]] constexpr std::uint8_t hub_depth() const noexcept {
        constexpr std::size_t kByteMax = std::numeric_limits<std::uint8_t>::max();
        return static_cast<std::uint8_t>(v < kByteMax ? v : kByteMax);
    }

    friend constexpr bool operator==(StackDepth, StackDepth) = default;
    friend constexpr auto operator<=>(StackDepth, StackDepth) = default;
};

using app::config_slot_of;
using app::CycleDir;
using app::dip_row_of;
using app::last_osd_row;
using app::osd_row_at;
using app::osd_rows_of;
using app::row_after;
using app::row_index_of;
using app::RowCount;
using app::RowIndex;

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

    constexpr RowRange(RowIndex first, RowCount count) noexcept : run_{first, count} {}

    [[nodiscard]] constexpr Iter begin() const noexcept { return Iter{run_.start}; }

    [[nodiscard]] constexpr Iter end() const noexcept { return Iter{run_.end()}; }

    [[nodiscard]] constexpr bool contains(RowIndex r) const noexcept { return run_.contains(r); }

private:
    infra::affine::Run<RowIndex> run_{};
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

enum class RowKeySpace : std::uint32_t {
    Item = 0x0,
    Save = 0x1,
    Push = 0x2,
    Slot = 0x4,
    Recent = 0x8,
};

struct RowDigest {
    std::uint32_t v = 0;
    friend constexpr bool operator==(RowDigest, RowDigest) = default;
};

struct RowKey {

    struct Ordinal {
        std::uint16_t v = 0;
        friend constexpr bool operator==(Ordinal, Ordinal) = default;
    };

    std::uint32_t v = 0;

    [[nodiscard]] static constexpr RowKey in_space(RowKeySpace space, Ordinal ordinal) noexcept {
        return RowKey{tag_of(space) | ordinal.v};
    }

    [[nodiscard]] static constexpr RowKey ordinal(proto::ItemOrdinal item) noexcept {
        return RowKey{tag_of(RowKeySpace::Item) | item.v};
    }

    [[nodiscard]] static constexpr RowKey save() noexcept {
        return in_space(RowKeySpace::Save, Ordinal{});
    }

    [[nodiscard]] static constexpr RowKey hashed(RowKeySpace space, RowDigest digest) noexcept {
        return RowKey{tag_of(space) | (digest.v & (tag_of(space) - 1u))};
    }

    friend constexpr bool operator==(RowKey, RowKey) = default;
    friend constexpr auto operator<=>(RowKey, RowKey) noexcept = default;

private:
    [[nodiscard]] static constexpr std::uint32_t tag_of(RowKeySpace space) noexcept {
        constexpr unsigned kOrdinalBits = 16;
        return static_cast<std::uint32_t>(space) << kOrdinalBits;
    }
};

struct RowKeyBounds {
    RowKey low;
    RowKey high;
};

[[nodiscard]] consteval RowKeyBounds row_key_bounds(RowKeySpace space) noexcept {
    constexpr RowKey::Ordinal kLastItem{std::numeric_limits<std::uint16_t>::max()};
    constexpr proto::ItemOrdinal kLastOrdinal{std::numeric_limits<std::uint16_t>::max()};
    constexpr RowDigest kLastHash{std::numeric_limits<std::uint32_t>::max()};
    switch (space) {
        case RowKeySpace::Item:
            return RowKeyBounds{
                RowKey::ordinal(proto::ItemOrdinal{}),
                RowKey::ordinal(kLastOrdinal)
            };
        case RowKeySpace::Save:
            return RowKeyBounds{RowKey::save(), RowKey::save()};
        case RowKeySpace::Push:
        case RowKeySpace::Slot:
            return RowKeyBounds{
                RowKey::in_space(space, RowKey::Ordinal{}),
                RowKey::in_space(space, kLastItem)
            };
        case RowKeySpace::Recent:
            return RowKeyBounds{
                RowKey::hashed(RowKeySpace::Recent, RowDigest{}),
                RowKey::hashed(RowKeySpace::Recent, kLastHash)
            };
    }
    return RowKeyBounds{};
}

[[nodiscard]] consteval bool row_key_spaces_are_disjoint() noexcept {
    constexpr std::array<RowKeySpace, 5> kSpaces{
        RowKeySpace::Item,
        RowKeySpace::Save,
        RowKeySpace::Push,
        RowKeySpace::Slot,
        RowKeySpace::Recent
    };
    for (std::size_t i = 0; i < kSpaces.size(); ++i) {
        for (std::size_t j = i + 1; j < kSpaces.size(); ++j) {
            const RowKeyBounds a = row_key_bounds(kSpaces[i]);
            const RowKeyBounds b = row_key_bounds(kSpaces[j]);
            if (a.low <= b.high && b.low <= a.high)
                return false;
        }
    }
    return true;
}

static_assert(row_key_spaces_are_disjoint());

static_assert(
    row_key_bounds(RowKeySpace::Recent).low > row_key_bounds(RowKeySpace::Push).high
    && row_key_bounds(RowKeySpace::Recent).low > row_key_bounds(RowKeySpace::Slot).high
);

struct MenuMask {
    std::uint64_t v = 0;

    static constexpr std::uint32_t kBits = 64;

    [[nodiscard]] constexpr bool contains(RowIndex r) const noexcept;

    [[nodiscard]] static constexpr bool addresses(RowIndex r) noexcept;

    [[nodiscard]] constexpr MenuMask with(RowIndex r) const noexcept;

    [[nodiscard]] constexpr MenuMask only(MenuMask subset) const noexcept;

    [[nodiscard]] constexpr MenuMask without(RowIndex r) const noexcept;

    [[nodiscard]] constexpr MenuMask without(MenuMask drop) const noexcept;

    [[nodiscard]] constexpr std::optional<RowIndex> lowest() const noexcept;

    [[nodiscard]] constexpr std::optional<RowIndex> highest() const noexcept;

    [[nodiscard]] constexpr std::optional<RowIndex> next_after(RowIndex r) const noexcept;

    [[nodiscard]] constexpr std::optional<RowIndex> prev_before(RowIndex r) const noexcept;

    friend constexpr bool operator==(MenuMask, MenuMask) = default;

private:
    using Bits = infra::IndexSet<RowIndex, kBits, std::uint64_t>;

    [[nodiscard]] constexpr Bits bits() const noexcept { return Bits{v}; }
};

[[nodiscard]] constexpr RowCount menu_mask_rows() noexcept {
    return RowCount{MenuMask::kBits};
}

constexpr bool MenuMask::addresses(RowIndex r) noexcept {
    return Bits::addresses(r);
}

[[nodiscard]] constexpr MenuMask dense_mask(RowCount n) noexcept {
    if (n >= menu_mask_rows())
        return MenuMask{~std::uint64_t{0}};
    return MenuMask{(std::uint64_t{1} << n.v) - 1u};
}

[[nodiscard]] constexpr MenuMask row_bit(RowIndex r) noexcept {
    return MenuMask{}.with(r);
}

constexpr bool MenuMask::contains(RowIndex r) const noexcept {
    return bits().contains(r);
}

constexpr MenuMask MenuMask::with(RowIndex r) const noexcept {
    return MenuMask{bits().with_or_unchanged(r).v};
}

constexpr MenuMask MenuMask::only(MenuMask subset) const noexcept {
    return MenuMask{bits().only(Bits{subset.v}).v};
}

constexpr MenuMask MenuMask::without(RowIndex r) const noexcept {
    return MenuMask{bits().without_or_unchanged(r).v};
}

constexpr MenuMask MenuMask::without(MenuMask drop) const noexcept {
    return MenuMask{bits().without(Bits{drop.v}).v};
}

constexpr std::optional<RowIndex> MenuMask::lowest() const noexcept {
    return bits().lowest();
}

constexpr std::optional<RowIndex> MenuMask::highest() const noexcept {
    return bits().highest();
}

constexpr std::optional<RowIndex> MenuMask::next_after(RowIndex r) const noexcept {
    if (r >= row_after(menu_mask_rows() - kOneRow))
        return std::nullopt;
    return without(dense_mask(r.before() + kOneRow)).lowest();
}

constexpr std::optional<RowIndex> MenuMask::prev_before(RowIndex r) const noexcept {
    return only(dense_mask(r.before())).highest();
}

struct UiButtonMask {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr bool intersects(UiButtonMask bits) const noexcept {
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
static_assert(MenuMask{0b100u}.contains(RowIndex{2}) && !MenuMask{0b100u}.contains(RowIndex{64}));
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
static_assert(MenuMask{0b1111u}.without(RowIndex{1}) == MenuMask{0b1101u});
static_assert(MenuMask{}.with(RowIndex{2}) == MenuMask{0b100u});
static_assert(!MenuMask{}.contains(RowIndex{2}) && MenuMask{0b100u}.contains(RowIndex{2}));
static_assert(sizeof(MenuMask) == 8);
static_assert(sizeof(CycleDir) == 1);

}
