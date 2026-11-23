// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>

#include "infra/seat.h"
#include "proto/types.h"
#include "svc/gcr.h"
#include "svc/mirror_piece.h"

namespace mister::svc {

class G64Synthesis {
    TASTY_SEAT_EXEMPT(component);

public:
    struct Read {
        proto::FileOffset at{};
        std::span<std::byte> into{};
    };

    G64Synthesis(proto::FileSize size, const gcr::DiskId& id) noexcept;

    [[nodiscard]] std::optional<Read> next_read() noexcept;

    [[nodiscard]] MirrorPiece track() noexcept;

    [[nodiscard]] MirrorPiece header() const noexcept;

private:
    enum Room : std::size_t { kTrackRoom = 8192 };

    [[nodiscard]] gcr::TrackTotal slots_() const noexcept;

    [[nodiscard]] std::span<std::byte, gcr::kTableEntryBytes> position_entry_(
        gcr::HalfTrack slot
    ) noexcept;
    [[nodiscard]] std::span<std::byte, gcr::kTableEntryBytes> speed_entry_(
        gcr::HalfTrack slot
    ) noexcept;

    gcr::SectorMap map_;
    gcr::DiskId id_;
    gcr::HalfTrack next_{};
    gcr::ImagePos free_{};
    std::array<std::byte, gcr::kSectorBufferBytes> sectors_{};
    std::array<std::byte, kTrackRoom> track_{};
    std::array<std::byte, gcr::TrackTotal::kSlots1571.header_bytes().file_word<std::size_t>()>
        header_{};
};

}
