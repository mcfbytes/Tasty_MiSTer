// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include "infra/seat.h"
#include "proto/types.h"
#include "svc/chd_geometry.h"
#include "svc/disc_identity.h"
#include "svc/seek_span.h"
#include "svc/toc.h"
#include "svc/types.h"

namespace mister::svc {

using proto::Lba;

[[nodiscard]] std::optional<TrackIndex> track_for_lba(const Toc& toc, Lba lba) noexcept;

using SeekTime = std::chrono::duration<std::uint32_t, std::milli>;
[[nodiscard]] SeekTime seek_time(SeekSpan span) noexcept;

struct DiscGeometry {

    TASTY_SEAT_EXEMPT(const_shared);

    Toc toc{};

    ChdGeometry chd{};
    bool mounted = false;

    DiscIdentity identity{};

    [[nodiscard]] std::optional<TrackIndex> track_for_lba(Lba lba) const noexcept {
        return svc::track_for_lba(toc, lba);
    }

    [[nodiscard]] SeekTime seek_time(SeekSpan span) const noexcept { return svc::seek_time(span); }

    [[nodiscard]] std::uint32_t chd_hunk_bytes() const noexcept { return chd.hunk_detail(); }
};

}
