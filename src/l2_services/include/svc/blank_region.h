// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/byte_view.h"
#include "infra/span_cut.h"
#include "proto/types.h"

namespace mister::svc {

struct BlankRegion {
    proto::Lba block;
    proto::FileSize within;
    std::span<const std::byte> bytes;

    [[nodiscard]] constexpr proto::FileOffset image_offset(
        proto::BlockSize lba_bytes
    ) const noexcept {
        return lba_bytes.position_of(block) + within;
    }

    void paste_into(
        proto::BlockSize lba_bytes,
        proto::FileOffset base,
        std::span<std::uint8_t> out
    ) const noexcept {
        if (bytes.empty() || out.empty())
            return;
        const proto::FileOffset at = image_offset(lba_bytes);
        const proto::FileOffset end = base + proto::FileSize::of(out);
        const proto::FileOffset run_end = at + proto::FileSize::of(bytes);
        if (run_end <= base || at >= end)
            return;
        const proto::FileOffset from = std::max(at, base);
        const proto::FileOffset to = std::min(run_end, end);
        const std::span<const std::byte> run = infra::affine::first(
            infra::affine::from(bytes, from - at),
            to - from
        );
        std::ranges::copy(
            infra::byte_cast<const std::uint8_t>(run),
            infra::affine::from(out, from - base).begin()
        );
    }
};

}
