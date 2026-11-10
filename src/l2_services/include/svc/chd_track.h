// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <utility>

#include "proto/types.h"
#include "svc/chd_meta_row.h"
#include "svc/track.h"
#include "svc/types.h"

namespace mister::svc {

enum class ChdSubcode : std::uint8_t { None = 0, Rw, RwRaw };

enum class ChdSectorSize : std::uint32_t {
    None = 0,
    Raw = kRawFrameBytes,
    UserData = kUserDataBytes,
    Mode2Cooked = 2336,
};

namespace detail {

[[nodiscard]] constexpr std::uint32_t chd_sector_bytes(ChdSectorSize size) noexcept {
    return std::to_underlying(size);
}

}

struct ChdTrack {
    TrackNumber number{};
    proto::BlockCount frames{};
    proto::BlockCount pregap{};
    proto::BlockCount postgap{};
    TrackType type = TrackType::Cdda;
    ChdSectorSize sector_size = ChdSectorSize::None;
    bool pregap_held = false;
    ChdSubcode subcode = ChdSubcode::None;

    void apply_type_to(Track& track) const noexcept;
};

[[nodiscard]] std::optional<ChdTrack> parse_chd_track(const ChdMetaRow& row) noexcept;

}
