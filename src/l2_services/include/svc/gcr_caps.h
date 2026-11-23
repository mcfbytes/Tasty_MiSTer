// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/types.h"
#include "svc/gcr_types.h"

namespace mister::svc {

struct GcrCaps {
    std::uint8_t v = 0;

    static const GcrCaps kDoubleSided;
    static const GcrCaps kGcr;
    static const GcrCaps kMfm;
    static const GcrCaps kHighDensity;

    static const GcrCaps kSectors1581;

    [[nodiscard]] static constexpr GcrCaps of_tracks(gcr::TrackTotal slots) noexcept;

    [[nodiscard]] static constexpr GcrCaps of_sectors(gcr::TrackTotal tracks) noexcept;

    [[nodiscard]] constexpr bool has(GcrCaps bits) const noexcept { return (v & bits.v) == bits.v; }

    [[nodiscard]] constexpr proto::WideIoIndex mount_word(proto::IoIndex slot) const noexcept {
        return proto::WideIoIndex::of(slot, v);
    }

    friend constexpr GcrCaps operator|(GcrCaps a, GcrCaps b) noexcept {
        return GcrCaps{static_cast<std::uint8_t>(a.v | b.v)};
    }

    friend constexpr bool operator==(GcrCaps, GcrCaps) = default;
};

inline constexpr GcrCaps GcrCaps::kDoubleSided{1};
inline constexpr GcrCaps GcrCaps::kGcr{2};
inline constexpr GcrCaps GcrCaps::kMfm{4};
inline constexpr GcrCaps GcrCaps::kHighDensity{8};
inline constexpr GcrCaps GcrCaps::kSectors1581 = GcrCaps::kHighDensity | GcrCaps::kDoubleSided;

constexpr GcrCaps GcrCaps::of_tracks(gcr::TrackTotal slots) noexcept {
    const GcrCaps raw = kGcr | kMfm;
    return slots > gcr::TrackTotal::kSlots1541 ? raw | kDoubleSided : raw;
}

constexpr GcrCaps GcrCaps::of_sectors(gcr::TrackTotal tracks) noexcept {
    return tracks > gcr::TrackTotal::kOneSide ? kGcr | kDoubleSided : kGcr;
}

static_assert(
    GcrCaps::of_tracks(gcr::TrackTotal{168}) == GcrCaps{7}
        && GcrCaps::of_tracks(gcr::TrackTotal{84}) == GcrCaps{6},
    "c64.cpp c64_openGCR: G64_SUPPORT_GCR | G64_SUPPORT_MFM, DS when tracks > 84"
);
static_assert(
    GcrCaps::of_sectors(gcr::TrackTotal{70}) == GcrCaps{3}
        && GcrCaps::of_sectors(gcr::TrackTotal{42}) == GcrCaps{2},
    "c64.cpp c64_openGCR: G64_SUPPORT_GCR, DS when tracks > 42"
);
static_assert(
    GcrCaps::kSectors1581 == GcrCaps{9},
    "user_io.cpp user_io_file_mount: a .d81 is G64_SUPPORT_HD | G64_SUPPORT_DS"
);
static_assert(
    GcrCaps{7}.mount_word(proto::IoIndex{1}) == proto::WideIoIndex{0x1C1},
    "user_io.cpp user_io_file_mount: user_io_set_aindex(img_type << 6 | index)"
);

}
