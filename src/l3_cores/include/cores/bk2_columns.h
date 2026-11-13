// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "cores/bk2_column.h"

namespace mister::cores {

inline constexpr std::size_t kMovieColumnsMax = 48;

struct ColumnCount {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr ColumnCount of(std::size_t n) noexcept {
        return ColumnCount{static_cast<std::uint8_t>(n)};
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return v; }

    template <class Cell, std::size_t N>
    [[nodiscard]] constexpr std::span<const Cell> head(
        const std::array<Cell, N>& cells
    ) const noexcept {
        return std::span<const Cell>(cells).first(v);
    }

    template <class Cell, std::size_t N>
    [[nodiscard]] constexpr std::array<Cell, N> placed(
        std::array<Cell, N> cells,
        const Cell& c
    ) const noexcept {
        cells[v] = c;
        return cells;
    }

    [[nodiscard]] constexpr bool within(ColumnCount limit) const noexcept { return v < limit.v; }

    [[nodiscard]] constexpr ColumnCount next() const noexcept {
        return ColumnCount{static_cast<std::uint8_t>(v + 1)};
    }

    friend constexpr bool operator==(ColumnCount, ColumnCount) = default;
    friend constexpr auto operator<=>(ColumnCount, ColumnCount) = default;
};

class Bk2Columns {
public:
    [[nodiscard]] constexpr ColumnCount count() const noexcept { return n_; }

    [[nodiscard]] constexpr std::span<const Bk2Column> view() const noexcept {
        return n_.head(cells_);
    }

    [[nodiscard]] constexpr std::optional<Bk2Columns> append(Bk2Column c) const noexcept {
        if (c.kind() == Bk2Column::Kind::Unset || !n_.within(kLimit))
            return std::nullopt;
        return with_appended(c);
    }

    [[nodiscard]] constexpr std::optional<Bk2Columns> append_keeping_group_end(
        Bk2Column c
    ) const noexcept {
        if (c.kind() == Bk2Column::Kind::Unset || !n_.next().within(kLimit))
            return std::nullopt;
        return with_appended(c);
    }

    friend constexpr bool operator==(const Bk2Columns&, const Bk2Columns&) = default;

private:
    [[nodiscard]] constexpr Bk2Columns with_appended(Bk2Column c) const noexcept {
        Bk2Columns r = *this;
        r.cells_ = r.n_.placed(r.cells_, c);
        r.n_ = r.n_.next();
        return r;
    }

    static constexpr ColumnCount kLimit = ColumnCount::of(kMovieColumnsMax);
    ColumnCount n_{};
    std::array<Bk2Column, kMovieColumnsMax> cells_{};
};

}
