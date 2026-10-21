// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/cd_transport.h"
#include "cores/cdwire.h"
#include "infra/error.h"
#include "svc/disc_engine.h"

namespace mister::cores {

namespace {

[[nodiscard]] inline Ex<void> send_frame(
    ICdTransport& tx,
    IoIndex idx,
    std::span<const std::uint8_t> f
) {
    return tx.send_data(idx, std::as_bytes(f));
}

constexpr std::size_t kUserData = 2048;
constexpr std::size_t kRawFrame = 2352;
constexpr std::size_t kSubBytes = 96;

using cdwire::bcd;
using cdwire::lba_to_msf;
using cdwire::Msf;

using svc::Lba;
using svc::TrackType;

inline std::uint32_t track_scan(const svc::Toc& toc, std::int32_t lba) noexcept {
    std::uint32_t idx = 0;
    while (idx < toc.last && static_cast<std::int64_t>(toc.tracks[idx].end.v) <= lba) {
        ++idx;
    }
    return idx;
}

}

}
