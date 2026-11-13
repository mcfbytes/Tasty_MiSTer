// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/msu_wire.h"
#include "infra/seat.h"
#include "svc/file.h"

namespace mister::cores {

class SectorReadAhead {
    TASTY_SEAT_RESIDENT(Pcm);

public:
    static constexpr std::size_t kAhead = 8;

    struct Tally {
        std::uint32_t ahead_hits = 0;
        std::uint32_t ahead_reads = 0;
        std::uint32_t sync_reads = 0;
        std::uint32_t read_errors = 0;
    };

    [[nodiscard]] std::span<std::uint8_t> take(
        svc::IFile& file,
        msu::TrackCursor cursor,
        std::span<std::uint8_t> dst
    ) noexcept;

    void seek(msu::TrackCursor cursor) noexcept;

    void refill_one(svc::IFile& file, proto::FileSize size) noexcept;

    [[nodiscard]] bool at_rest(proto::FileSize size) const noexcept;

    void drop(msu::TrackCursor from) noexcept;

    [[nodiscard]] const Tally& tally() const noexcept { return tally_; }

private:
    struct Sector {
        msu::TrackCursor off{};
        std::uint16_t got = 0;
        std::array<std::uint8_t, msu::kSectorBytes> bytes{};
    };

    [[nodiscard]] std::span<std::uint8_t> read_(
        svc::IFile& file,
        msu::TrackCursor off,
        std::span<std::uint8_t> dst
    ) noexcept;

    std::array<Sector, kAhead> ahead_{};
    std::size_t head_ = 0;
    std::size_t held_ = 0;
    msu::TrackCursor fill_at_{};
    bool eof_ = false;
    Tally tally_{};
};

}
