// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/cd_link.h"
#include "cores/cd_msf.h"
#include "cores/cdwire.h"
#include "cores/types.h"
#include "infra/error.h"
#include "svc/disc_engine.h"

namespace mister::cores {

namespace {

[[nodiscard]] inline Ex<void> send_frame(
    ICdLink& link,
    IoIndex idx,
    std::span<const std::uint8_t> f
) {
    return link.send_data(idx, std::as_bytes(f));
}

constexpr std::size_t kUserData = 2048;
constexpr std::size_t kRawFrame = svc::kCdDataSize;
constexpr std::size_t kSubBytes = 96;

inline constexpr SectorCount kPregapSectors = SectorCount::of(svc::kRedBookPregap);

using cdwire::bcd;
using svc::Msf;

using svc::Lba;
using svc::TrackType;

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

inline svc::TrackIndex track_scan(const svc::Toc& toc, HeadLba lba) noexcept {
    svc::TrackIndex idx{};
    while (toc.last.contains(idx) && head_of(toc.at(idx).end) <= lba) {
        idx = idx + svc::TrackCount{1};
    }
    return idx;
}

}

}
