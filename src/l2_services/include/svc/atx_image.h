// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "infra/error.h"
#include "infra/indexed_array.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/atx_types.h"
#include "svc/file.h"
#include "svc/sector_image.h"
#include "svc/sector_types.h"

namespace mister::svc {

class AtxImage final : public ISectorImage {
    TASTY_SEAT_RESIDENT(Pcm);

public:
    [[nodiscard]] static Ex<std::unique_ptr<ISectorImage>> open(
        std::unique_ptr<IFile> file,
        bool writable
    );

    [[nodiscard]] SectorGeometry geometry() const noexcept override;
    [[nodiscard]] bool writable() const noexcept override;
    [[nodiscard]] Ex<SectorRead> read_sector(
        const SectorAsk& ask,
        std::span<std::byte> dst
    ) override;
    [[nodiscard]] Ex<SectorRead> write_sector(
        const SectorAsk& ask,
        std::span<const std::byte> src
    ) override;

    [[nodiscard]] Ex<void> clear() override;

    static constexpr AtxTrackCount kTracks{42};

    enum class Density : std::uint8_t { Single = 0, Medium = 1, Double = 2 };

    using Track = AtxTrack;

    using TrackPlaces = infra::IndexedArray<std::optional<AtxOffset>, kTracks.v, Track>;

private:
    enum class Op : std::uint8_t { Read, Write };

    struct Transfer {
        Op op = Op::Read;
        std::span<std::byte> into{};
        std::span<const std::byte> from{};
    };

    struct Place {
        AtxTrack track{};
        AtxSectorNumber number{};
    };

    AtxImage(
        std::unique_ptr<IFile> file,
        bool writable,
        Density density,
        const TrackPlaces& tracks
    ) noexcept
        : file_(std::move(file)),
          writable_(writable),
          density_(density),
          tracks_(tracks) {}

    [[nodiscard]] SectorRead transfer_(const SectorAsk& ask, const Transfer& t);
    [[nodiscard]] SectorCount sectors_per_track_() const noexcept;
    [[nodiscard]] SectorSize sector_bytes_() const noexcept;
    [[nodiscard]] Place place_of_(SectorIndex sector) const noexcept;
    [[nodiscard]] std::optional<AtxOffset> track_at_(Track track) const noexcept;

    std::unique_ptr<IFile> file_;
    bool writable_;
    Density density_;
    TrackPlaces tracks_;
    Track head_track_{};
    AtxNoise noise_ = AtxNoise::kSeed;
};

}
