// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include "infra/seat.h"
#include "proto/types.h"
#include "svc/toc.h"
#include "svc/types.h"

namespace mister::svc {

using proto::Lba;

[[nodiscard]] std::optional<TrackIndex> track_for_lba(const Toc& toc, Lba lba) noexcept;

using SeekTime = std::chrono::duration<std::uint32_t, std::milli>;
[[nodiscard]] SeekTime seek_time(Lba from_lba, Lba to_lba) noexcept;

struct DiscGeometry {
    TASTY_SEAT_RESIDENT(RT);

    Toc toc{};

    std::uint32_t hunk_bytes = 0;
    bool mounted = false;

    [[nodiscard]] std::optional<TrackIndex> track_for_lba(Lba lba) const noexcept {
        return svc::track_for_lba(toc, lba);
    }

    [[nodiscard]] SeekTime seek_time(Lba from_lba, Lba to_lba) const noexcept {
        return svc::seek_time(from_lba, to_lba);
    }

    [[nodiscard]] std::uint32_t chd_hunk_bytes() const noexcept { return hunk_bytes; }
};

}
