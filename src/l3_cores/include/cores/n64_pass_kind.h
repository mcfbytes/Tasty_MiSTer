// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <utility>

#include "cores/row_shape_key.h"

namespace mister::cores::n64 {

enum class PassKind : std::uint8_t { Rom = 0, Ipl, Disk };

[[nodiscard]] constexpr std::optional<PassKind> pass_kind_of(RowShapeKey key) noexcept {
    switch (key.v) {
        case std::to_underlying(PassKind::Rom):
            return PassKind::Rom;
        case std::to_underlying(PassKind::Ipl):
            return PassKind::Ipl;
        case std::to_underlying(PassKind::Disk):
            return PassKind::Disk;
    }
    return std::nullopt;
}

[[nodiscard]] constexpr RowShapeKey shape_key_of(PassKind kind) noexcept {
    return RowShapeKey{std::to_underlying(kind)};
}

}
