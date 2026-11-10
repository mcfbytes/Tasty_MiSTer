// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mister::proto {

enum class AxiKind : std::uint8_t { Lw, Ddr, Unknown };

struct AxiKindToken {
    AxiKind kind;
    std::string_view token;
};

constexpr std::array<AxiKindToken, 2> kAxiKindTokens{{
    {AxiKind::Lw, "LW"},
    {AxiKind::Ddr, "DDR"},
}};

[[nodiscard]] constexpr AxiKind axi_kind_of(std::string_view token) noexcept {
    for (const AxiKindToken& row : kAxiKindTokens) {
        if (row.token == token)
            return row.kind;
    }
    return AxiKind::Unknown;
}

consteval bool axi_kind_rows_join() noexcept {
    constexpr std::array<AxiKind, 2> kDeclarable{{AxiKind::Lw, AxiKind::Ddr}};
    for (const AxiKind kind : kDeclarable) {
        std::size_t rows = 0;
        for (const AxiKindToken& row : kAxiKindTokens) {
            if (row.kind == kind)
                ++rows;
        }
        if (rows != 1)
            return false;
    }
    for (const AxiKindToken& row : kAxiKindTokens) {
        if (row.kind == AxiKind::Unknown || row.token.empty())
            return false;
    }
    return true;
}

static_assert(axi_kind_rows_join(), "AXI kind tokens: one row per declarable kind");

}
