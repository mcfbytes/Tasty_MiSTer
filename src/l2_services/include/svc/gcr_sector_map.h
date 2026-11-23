// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/gcr_types.h"

namespace mister::svc::gcr {

class SectorMap {
    TASTY_SEAT_EXEMPT(component);

public:
    [[nodiscard]] static constexpr SectorMap of_image(proto::FileSize size) noexcept;

    [[nodiscard]] constexpr TrackTotal tracks() const noexcept { return tracks_; }

    [[nodiscard]] constexpr proto::BlockCount sectors_on(FullTrack track) const noexcept {
        if (track >= kEnd)
            return proto::BlockCount{};
        return first_[track.next()] - first_[track];
    }

    [[nodiscard]] constexpr proto::FileOffset position_of(FullTrack track) const noexcept {
        return kSector.position_of(first_[track]);
    }

    [[nodiscard]] constexpr proto::BlockCount sectors() const noexcept {
        return first_[kEnd] - proto::Lba{};
    }

    static constexpr proto::BlockSize kSector{256};

private:
    enum Extent : std::size_t { kFullTracks = 84, kForms = 4 };

    static constexpr FullTrack kEnd{kFullTracks};

    static constexpr proto::BlockSize kSectorWithError{257};

    constexpr SectorMap(TrackTotal side, bool two_sides) noexcept
        : tracks_(two_sides ? side.doubled() : side) {
        proto::Lba sector{};
        for (FullTrack t{}; t <= kEnd; t = t.next()) {
            first_[t] = sector;
            const bool held = t.second_side() ? two_sides && side.holds(t.on_side())
                                              : side.holds(t);
            if (held)
                sector = sector + sectors_of_zone(t.on_side());
        }
    }

    [[nodiscard]] static constexpr SectorMap one_sided(TrackTotal side) noexcept {
        return SectorMap{side, false};
    }

    [[nodiscard]] static constexpr SectorMap two_sided(TrackTotal side) noexcept {
        return SectorMap{side, true};
    }

    [[nodiscard]] static constexpr proto::BlockCount sectors_of_zone(FullTrack on_side) noexcept {
        constexpr FullTrack kZone3End{17};
        constexpr FullTrack kZone2End{24};
        constexpr FullTrack kZone1End{30};
        constexpr proto::BlockCount kZone3{21};
        constexpr proto::BlockCount kZone2{19};
        constexpr proto::BlockCount kZone1{18};
        constexpr proto::BlockCount kZone0{17};
        return on_side < kZone3End ? kZone3
            : on_side < kZone2End  ? kZone2
            : on_side < kZone1End  ? kZone1
                                   : kZone0;
    }

    [[nodiscard]] static constexpr std::array<SectorMap, kForms> forms() noexcept {
        constexpr TrackTotal k35{35};
        constexpr TrackTotal k40{40};
        constexpr TrackTotal k42{42};
        return {one_sided(k35), one_sided(k40), one_sided(k42), two_sided(k35)};
    }

    TrackTotal tracks_;

    infra::IndexedArray<proto::Lba, kFullTracks + 1, FullTrack> first_{};
};

constexpr SectorMap SectorMap::of_image(proto::FileSize size) noexcept {
    const std::array<SectorMap, kForms> all = forms();
    for (const SectorMap& form : all) {
        if (size <= kSectorWithError.bytes_of(form.sectors()))
            return form;
    }
    return all.back();
}

static_assert(
    SectorMap::of_image(proto::FileSize{174848}).tracks() == TrackTotal{35}
        && SectorMap::of_image(proto::FileSize{175531}).tracks() == TrackTotal{35}
        && SectorMap::of_image(proto::FileSize{175532}).tracks() == TrackTotal{40},
    "c64.cpp c64_openGCR: f->size <= sector_map[84] * 257, 683 sectors for 35 tracks"
);
static_assert(
    SectorMap::of_image(proto::FileSize{196608}).sectors() == proto::BlockCount{768}
        && SectorMap::of_image(proto::FileSize{205312}).sectors() == proto::BlockCount{802}
        && SectorMap::of_image(proto::FileSize{349696}).sectors() == proto::BlockCount{1366},
    "c64.cpp start_sectors: 768, 802 and 1366 sectors for 40, 42 and 2 x 35 tracks"
);
static_assert(
    SectorMap::of_image(proto::FileSize{}).position_of(FullTrack{17}) == proto::FileOffset{0x16500}
        && SectorMap::of_image(proto::FileSize{}).sectors_on(FullTrack{17}) == proto::BlockCount{19}
        && SectorMap::of_image(proto::FileSize{}).sectors_on(FullTrack{35}) == proto::BlockCount{},
    "c64.cpp start_sectors[0]: track 18 starts at sector 357 and holds 19"
);
static_assert(
    SectorMap::of_image(proto::FileSize{349696}).sectors_on(FullTrack{41}) == proto::BlockCount{}
        && SectorMap::of_image(proto::FileSize{349696}).position_of(FullTrack{43})
            == proto::FileOffset{704 * 256}
        && SectorMap::of_image(proto::FileSize{349696}).sectors_on(FullTrack{76})
            == proto::BlockCount{17}
        && SectorMap::of_image(proto::FileSize{349696}).sectors_on(FullTrack{77})
            == proto::BlockCount{},
    "c64.cpp start_sectors[3]: side two is full tracks 42..76, 683 sectors in"
);

}
