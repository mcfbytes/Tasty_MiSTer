// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "hal/types.h"
#include "infra/affine.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/disk_image.h"
#include "svc/file.h"
#include "svc/types.h"

namespace mister::svc {

class AdfCodec final : public IDiskImage {
    TASTY_SEAT_RESIDENT(Io);

public:
    struct TrackCount {
        std::uint8_t v = 0;
        using affine_count = TrackCount;

        [[nodiscard]] static constexpr TrackCount of_file(FileSize file) noexcept;

        [[nodiscard]] static constexpr TrackCount of_stream(FileSize stream) noexcept;
        [[nodiscard]] constexpr FileSize stream_bytes() const noexcept;

        [[nodiscard]] constexpr bool none() const noexcept { return v == 0; }

        friend constexpr bool operator==(TrackCount, TrackCount) = default;
        friend constexpr auto operator<=>(TrackCount, TrackCount) = default;
    };

    struct Track {
        std::uint8_t v = 0;
        using affine_count = TrackCount;
        using affine_index = Track;

        [[nodiscard]] static constexpr Track of(hal::SpiWord word) noexcept {
            return Track{word.low_byte()};
        }

        [[nodiscard]] static constexpr Track holding(FileSize from_start) noexcept;

        [[nodiscard]] constexpr Track within(TrackCount count) const noexcept {
            return on(count) ? *this : infra::affine::end_of<Track>(count) - TrackCount{1};
        }

        [[nodiscard]] constexpr bool on(TrackCount count) const noexcept {
            return infra::affine::in_bounds(count, *this);
        }

        [[nodiscard]] constexpr FileSize stream_from() const noexcept;
        [[nodiscard]] constexpr FileSize file_from() const noexcept;

        [[nodiscard]] constexpr std::byte field() const noexcept { return std::byte{v}; }

        friend constexpr bool operator==(Track, Track) = default;
    };

    struct Sector {
        std::uint8_t v = 0;

        [[nodiscard]] static constexpr std::optional<Sector> holding(FileSize in_track) noexcept;

        [[nodiscard]] constexpr bool last() const noexcept { return v + 1u == kSectors; }

        [[nodiscard]] constexpr Sector next() const noexcept {
            return last() ? Sector{} : Sector{static_cast<std::uint8_t>(v + 1u)};
        }

        [[nodiscard]] constexpr bool on_track() const noexcept { return v < kSectors; }

        [[nodiscard]] constexpr std::byte field() const noexcept { return std::byte{v}; }

        [[nodiscard]] constexpr std::byte to_gap_field() const noexcept {
            return std::byte{static_cast<std::uint8_t>(kSectors - v)};
        }

        [[nodiscard]] constexpr FileSize stream_into() const noexcept;
        [[nodiscard]] constexpr FileSize file_into() const noexcept;
        friend constexpr bool operator==(Sector, Sector) = default;
    };

    static constexpr std::uint8_t kSectors = 11;

    static constexpr proto::FileSize kSectorFileBytes{512};

    static constexpr proto::FileSize kSectorBytes{1088};
    static constexpr proto::FileSize kGapBytes{700};
    static constexpr proto::FileSize kTrackBytes{12668};

    static constexpr TrackCount kMostTracks{166};
    static constexpr proto::FileSize kTrackFileBytes{5632};

    static constexpr hal::SpiWord kFillWord{0xAAAA};
    static constexpr hal::SpiWord kSyncWord{0x4489};

    static constexpr proto::FileSize kSyncAt{4};
    static constexpr proto::FileSize kSyncBytes{4};
    static constexpr proto::FileSize kDataFieldAt{56};

    static constexpr std::size_t kHeaderWords = 25;
    static constexpr std::size_t kDataWords = 516;

    using SectorFile = std::array<std::byte, kSectorFileBytes.file_word<std::size_t>()>;
    using EncodedSector = std::array<std::byte, kSectorBytes.file_word<std::size_t>()>;
    using HeaderWords = std::span<const hal::SpiWord, kHeaderWords>;
    using DataWords = std::span<const hal::SpiWord, kDataWords>;

    enum class HeaderFault : std::uint8_t {
        None,
        SecondSync,
        Format,
        TrackRange,
        SectorRange,
        GapRange,
        Checksum,
    };

    struct Header {
        HeaderFault fault = HeaderFault::None;
        Track track{};
        Sector sector{};
    };

    [[nodiscard]] static Ex<std::unique_ptr<IDiskImage>> open(
        std::unique_ptr<IFile> file,
        FileSize size,
        bool writable
    );

    [[nodiscard]] TrackCount tracks() const noexcept { return tracks_; }

    [[nodiscard]] static constexpr proto::FileOffset sector_at(Track t, Sector s) noexcept {
        return proto::kFileStart + t.stream_from() + s.stream_into();
    }

    [[nodiscard]] static constexpr proto::FileOffset gap_at(Track t) noexcept {
        return proto::kFileStart + t.stream_from() + kSectorBytes * std::uint64_t{kSectors};
    }

    [[nodiscard]] static EncodedSector encode(Track t, Sector s, const SectorFile& data) noexcept;

    [[nodiscard]] static Header read_header(HeaderWords words) noexcept;

    [[nodiscard]] static bool read_data(DataWords words, SectorFile& out) noexcept;

    [[nodiscard]] FileSize size() const noexcept override;
    [[nodiscard]] bool writable() const noexcept override;
    [[nodiscard]] Ex<infra::ByteCount> read_at(
        proto::FileOffset at,
        std::span<std::byte> dst
    ) override;

    [[nodiscard]] Ex<infra::ByteCount> write_at(
        proto::FileOffset at,
        std::span<const std::byte> src
    ) override;

private:
    struct Cached {
        Track track{};
        Sector sector{};
        EncodedSector bytes{};
    };

    AdfCodec(std::unique_ptr<IFile> file, TrackCount tracks, bool writable) noexcept;

    [[nodiscard]] Ex<const EncodedSector*> sector_(Track t, Sector s);

    [[nodiscard]] static constexpr proto::FileOffset file_at(Track t, Sector s) noexcept {
        return proto::kFileStart + t.file_from() + s.file_into();
    }

    std::unique_ptr<IFile> file_;
    TrackCount tracks_;
    bool writable_;
    std::optional<Cached> cached_{};
};

constexpr AdfCodec::TrackCount AdfCodec::TrackCount::of_file(FileSize file) noexcept {
    return TrackCount{static_cast<std::uint8_t>(
        std::min(file.whole_records(kTrackFileBytes), std::uint64_t{kMostTracks.v})
    )};
}

constexpr AdfCodec::TrackCount AdfCodec::TrackCount::of_stream(FileSize stream) noexcept {
    return TrackCount{static_cast<std::uint8_t>(stream.whole_records(kTrackBytes))};
}

constexpr FileSize AdfCodec::TrackCount::stream_bytes() const noexcept {
    return kTrackBytes * std::uint64_t{v};
}

constexpr AdfCodec::Track AdfCodec::Track::holding(FileSize from_start) noexcept {
    return Track{static_cast<std::uint8_t>(from_start.whole_records(kTrackBytes))};
}

constexpr FileSize AdfCodec::Track::stream_from() const noexcept {
    return kTrackBytes * std::uint64_t{v};
}

constexpr FileSize AdfCodec::Track::file_from() const noexcept {
    return kTrackFileBytes * std::uint64_t{v};
}

constexpr std::optional<AdfCodec::Sector> AdfCodec::Sector::holding(FileSize in_track) noexcept {
    if (in_track.whole_records(kSectorBytes) >= kSectors)
        return std::nullopt;
    return Sector{static_cast<std::uint8_t>(in_track.whole_records(kSectorBytes))};
}

constexpr FileSize AdfCodec::Sector::stream_into() const noexcept {
    return kSectorBytes * std::uint64_t{v};
}

constexpr FileSize AdfCodec::Sector::file_into() const noexcept {
    return kSectorFileBytes * std::uint64_t{v};
}

}
