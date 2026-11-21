// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/serial.h"

namespace mister::infra {

struct CellGen {
    std::uint32_t v = 0;
    using serial_rule = serial::SkipZero<std::uint32_t>;

    [[nodiscard]] constexpr bool held() const noexcept { return v != 0; }

    template <class Reader>
    [[nodiscard]] static constexpr CellGen seen_by(const Reader& r) noexcept {
        return r.seen();
    }

    template <class Cell>
    [[nodiscard]] bool is_current_in(const Cell& cell) const noexcept {
        return cell.generation() == *this;
    }

    friend constexpr bool operator==(CellGen, CellGen) = default;
};

static_assert(serial::Label<CellGen>, "CellGen opts into the serial-label zero rule");
static_assert(
    sizeof(CellGen) == 4 && std::is_trivially_copyable_v<CellGen>,
    "the generation crosses the seat boundary as one lock-free word"
);

}
