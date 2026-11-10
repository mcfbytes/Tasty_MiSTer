// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "proto/types.h"
#include "svc/types.h"

namespace mister::svc {

struct ChdUnit {
    std::uint32_t v = 0;
    friend constexpr bool operator==(ChdUnit, ChdUnit) = default;
};

struct ChdUnitInHunk {
    HunkIndex hunk;
    proto::BlockCount ahead;
};

[[nodiscard]] constexpr ChdUnitInHunk split_by(ChdUnit unit, proto::BlockCount per_hunk) noexcept {
    return ChdUnitInHunk{
        .hunk = HunkIndex{unit.v / per_hunk.v},
        .ahead = proto::BlockCount{unit.v % per_hunk.v}
    };
}

struct ChdGeometry {
    proto::FileSize hunk_bytes{};
    proto::FileSize unit_bytes{};
    HunkCount hunk_count{};

    [[nodiscard]] constexpr proto::BlockCount sectors_per_hunk() const noexcept {
        return hunk_bytes.whole_units(unit_bytes);
    }

    [[nodiscard]] constexpr HunkIndex hunk_of(ChdUnit unit) const noexcept {
        return split_by(unit, sectors_per_hunk()).hunk;
    }

    [[nodiscard]] constexpr std::size_t offset_in_hunk(ChdUnit unit) const noexcept {
        return unit_bytes.length_of(split_by(unit, sectors_per_hunk()).ahead);
    }

    [[nodiscard]] constexpr std::size_t hunk_extent() const noexcept {
        return static_cast<std::size_t>(hunk_bytes.v);
    }

    [[nodiscard]] constexpr std::size_t extent_of(HunkCount n) const noexcept {
        return n.bytes_at(hunk_extent());
    }

    [[nodiscard]] constexpr std::uint32_t hunk_detail() const noexcept {
        return static_cast<std::uint32_t>(hunk_bytes.v);
    }

    [[nodiscard]] constexpr std::uint32_t unit_detail() const noexcept {
        return static_cast<std::uint32_t>(unit_bytes.v);
    }

    friend constexpr bool operator==(ChdGeometry, ChdGeometry) = default;
};

}
