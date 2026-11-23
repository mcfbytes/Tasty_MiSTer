// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include "cores/window_shape.h"
#include "proto/block_geometry.h"
#include "proto/types.h"

namespace mister::cores {

struct LbaWindowRow {
    proto::SlotIndex slot{};
    proto::Lba first{};
    proto::BlockCount blocks{};
    proto::BlockSize block_size{0};
    proto::BlockCount window_blocks{};
    WindowShape shape = WindowShape::Data;
    proto::WindowPolicy policy = proto::WindowPolicy::Image;

    [[nodiscard]] constexpr proto::Lba end() const noexcept { return first + blocks; }

    [[nodiscard]] constexpr bool holds(proto::SlotIndex at, proto::Lba lba) const noexcept {
        return at == slot && first <= lba && lba < end();
    }

    [[nodiscard]] constexpr proto::BlockSize served_size(proto::BlockSize wire) const noexcept {
        return block_size.empty() ? wire : block_size;
    }

    [[nodiscard]] constexpr proto::BlockGeometry geometry(
        proto::BlockGeometry wire
    ) const noexcept {
        return proto::BlockGeometry{
            .block_size = served_size(wire.block_size),
            .window_blocks = window_blocks,
            .policy = policy
        };
    }

    [[nodiscard]] constexpr proto::FileOffset first_byte() const noexcept {
        return served_size(proto::BlockSize{proto::kDefaultBlockBytes}).position_of(first);
    }

    [[nodiscard]] constexpr proto::FileOffset end_byte() const noexcept {
        return served_size(proto::BlockSize{proto::kDefaultBlockBytes}).position_of(end());
    }

    [[nodiscard]] constexpr bool holds_byte(proto::FileOffset at) const noexcept {
        return first_byte() <= at && at < end_byte();
    }
};

[[nodiscard]] constexpr bool lba_windows_joined(std::span<const LbaWindowRow> rows) noexcept {
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const LbaWindowRow& row = rows[i];
        if (row.blocks == proto::BlockCount{} || row.end() <= row.first)
            return false;
        const bool data = row.shape == WindowShape::Data;
        if (data != (row.policy == proto::WindowPolicy::Image))
            return false;
        if (data && (row.first != proto::Lba{} || !row.block_size.empty()))
            return false;
        const proto::BlockSize size = row.served_size(proto::BlockSize{proto::kDefaultBlockBytes});
        if (size > proto::BlockSize{proto::kBlockStagingBytes})
            return false;
        if (size.bytes_of(row.window_blocks) > proto::FileSize{proto::kBlockStagingBytes})
            return false;
        for (std::size_t j = i + 1; j < rows.size(); ++j) {
            const LbaWindowRow& later = rows[j];
            if (later.slot != row.slot)
                continue;
            if (later.first < row.end())
                return false;
            const bool apart = later.first_byte() >= row.end_byte()
                || later.end_byte() <= row.first_byte();
            if (!apart)
                return false;
        }
    }
    return true;
}

}
