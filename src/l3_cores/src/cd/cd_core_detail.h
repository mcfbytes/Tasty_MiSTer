// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/cd/cd_link.h"
#include "cores/cd_msf.h"
#include "cores/cd/cdwire.h"
#include "cores/types.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "svc/msf.h"
#include "svc/disc/toc.h"
#include "svc/disc/track.h"
#include "svc/types.h"

namespace mister::cores::cd {

namespace {

[[nodiscard]] inline Ex<void> send_frame(
    ICdLink& link,
    IoIndex idx,
    std::span<const std::uint8_t> f
) {
    return link.send_data(idx, std::as_bytes(f));
}

inline constexpr SectorCount kPregapSectors = SectorCount::of(svc::disc::kRedBookPregap);

using cdwire::bcd;
using svc::Msf;

using svc::disc::Lba;
using svc::disc::TrackType;

[[nodiscard]] inline HeadLba head_of(Lba lba) noexcept {
    return HeadLba::of(lba);
}

[[nodiscard]] inline Msf msf_of(HeadLba head) noexcept {
    return head.msf();
}

[[nodiscard]] inline Msf stock_msf_of(HeadLba head) noexcept {
    return head.stock_msf();
}

[[nodiscard]] inline Msf stock_msf_of(SectorCount n) noexcept {
    return n.stock_msf();
}

inline svc::TrackIndex track_scan(const svc::disc::Toc& toc, HeadLba lba) noexcept {
    svc::TrackIndex idx{};
    while (toc.last.contains(idx) && head_of(toc.tracks[idx].end) <= lba) {
        idx = idx + svc::TrackCount{1};
    }
    return idx;
}

}

}
