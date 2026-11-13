// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/byte_view.h"
#include "proto/types.h"

namespace mister::svc {

struct BlankRegion {
    proto::Lba block;
    proto::FileSize within;
    std::span<const std::byte> bytes;

    [[nodiscard]] constexpr proto::FileSize image_offset(
        proto::BlockSize lba_bytes
    ) const noexcept {
        return proto::byte_offset(block, lba_bytes) + within;
    }

    void paste_into(
        proto::BlockSize lba_bytes,
        proto::FileSize base,
        std::span<std::uint8_t> out
    ) const noexcept {
        if (bytes.empty() || out.empty())
            return;
        const proto::FileSize at = image_offset(lba_bytes);
        const proto::FileSize end = base + proto::FileSize::of(out);
        const proto::FileSize run_end = at + proto::FileSize::of(bytes);
        if (run_end <= base || at >= end)
            return;
        const proto::FileSize from = std::max(at, base);
        const proto::FileSize to = std::min(run_end, end);
        const std::span<const std::byte> run = (to - from).first_of((from - at).skip_in(bytes));
        std::ranges::copy(
            infra::byte_cast<const std::uint8_t>(run),
            (from - base).skip_in(out).begin()
        );
    }
};

}
