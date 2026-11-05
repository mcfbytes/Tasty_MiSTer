// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/disc_geometry.h"

#include <cstddef>
#include <iterator>

namespace mister::svc {

namespace {

struct SectorGroup {
    std::int32_t per_rev;
    std::int32_t start;
    std::int32_t end;
    float rotation_ms;
};

constexpr SectorGroup kSectorGroups[] = {
    {10, 0, 12572, 133.47f},
    {11, 12573, 30244, 146.82f},
    {12, 30245, 49523, 160.17f},
    {13, 49524, 70408, 173.51f},
    {14, 70409, 92900, 186.86f},
    {15, 92901, 116998, 200.21f},
    {16, 116999, 142703, 213.56f},
    {17, 142704, 170014, 226.90f},
    {18, 170015, 198932, 240.25f},
    {19, 198933, 229456, 253.60f},
    {20, 229457, 261587, 266.95f},
    {21, 261588, 295324, 280.29f},
    {22, 295325, 330668, 293.64f},
    {23, 330669, 333012, 306.99f},
};
constexpr std::size_t kNumSectorGroups = std::size(kSectorGroups);

std::size_t find_group(std::int32_t sector) noexcept {
    for (std::size_t i = 0; i < kNumSectorGroups; ++i) {
        if (sector >= kSectorGroups[i].start && sector <= kSectorGroups[i].end)
            return i;
    }
    return 0;
}

}

std::optional<TrackIndex> track_for_lba(const Toc& toc, Lba lba) noexcept {

    for (std::uint8_t i = 0; i < toc.last && i < kMaxTracks; ++i) {
        if (lba < toc.tracks[i].end)
            return TrackIndex{i};
    }
    return std::nullopt;
}

SeekTime seek_time(Lba from_lba, Lba to_lba) noexcept {

    constexpr std::uint32_t kMaxSector = 360000;
    constexpr double kTracksPerGroupGap = 1606.48;
    constexpr std::int32_t kSettleSectors = 3;
    constexpr std::int32_t kShortJumpSectors = 7;
    constexpr float kSettleMs = 33.0f;
    constexpr float kShortJumpMs = 150.0f;
    constexpr float kNearMs = 283.0f;
    constexpr float kMidMs = 366.0f;
    constexpr float kFarMs = 600.0f;
    constexpr float kNearTracks = 80.0f;
    constexpr float kMidTracks = 160.0f;
    constexpr float kMidTracksNext = 161.0f;
    constexpr float kFarTracks = 644.0f;
    constexpr double kRotShare3q = 0.75;
    constexpr double kRotShareHalf = 0.5;
    constexpr double kTrackStepMs = 16.66;
    constexpr double kMidSpan = 80.0;
    constexpr double kFarSpan = 195.0;
    if (from_lba.v > kMaxSector || to_lba.v > kMaxSector)
        return SeekTime{0};

    const std::int32_t from = static_cast<std::int32_t>(from_lba.v);
    const std::int32_t to = static_cast<std::int32_t>(to_lba.v);
    const std::size_t si = find_group(from);
    const std::size_t ti = find_group(to);

    float track_difference = 0.0f;
    if (ti == si) {
        const std::int32_t d = (to > from) ? to - from : from - to;
        track_difference = static_cast<float>(d / kSectorGroups[ti].per_rev);
    } else if (ti > si) {
        track_difference = static_cast<float>(
            (kSectorGroups[si].end - from) / kSectorGroups[si].per_rev
        );
        track_difference += static_cast<float>(
            (to - kSectorGroups[ti].start) / kSectorGroups[ti].per_rev
        );
        track_difference = static_cast<float>(
            static_cast<double>(track_difference)
            + kTracksPerGroupGap * static_cast<double>(ti - si - 1)
        );
    } else {
        track_difference = static_cast<float>(
            (from - kSectorGroups[si].start) / kSectorGroups[si].per_rev
        );
        track_difference += static_cast<float>(
            (kSectorGroups[ti].end - to) / kSectorGroups[ti].per_rev
        );
        track_difference = static_cast<float>(
            static_cast<double>(track_difference)
            + kTracksPerGroupGap * static_cast<double>(si - ti - 1)
        );
    }

    const std::int32_t delta = (to > from) ? to - from : from - to;
    const float rot = kSectorGroups[ti].rotation_ms;

    const double rotd = static_cast<double>(rot);
    float ms = 0.0f;
    if (delta <= kSettleSectors) {
        ms = kSettleMs;
    } else if (delta < kShortJumpSectors) {
        ms = kShortJumpMs + static_cast<float>(rotd * kRotShare3q);
    } else if (track_difference <= kNearTracks) {
        ms = kNearMs + static_cast<float>(rotd * kRotShare3q);
    } else if (track_difference <= kMidTracks) {
        ms = kMidMs + static_cast<float>(rotd * kRotShare3q);
    } else if (track_difference <= kFarTracks) {
        ms = kMidMs
            + static_cast<float>(rotd * kRotShare3q)
            + static_cast<float>(
                 static_cast<double>(track_difference - kMidTracksNext) * kTrackStepMs / kMidSpan
            );
    } else {
        ms = kFarMs
            + static_cast<float>(rotd * kRotShareHalf)
            + static_cast<float>(
                 static_cast<double>(track_difference - kFarTracks) * kTrackStepMs / kFarSpan
            );
    }

    return SeekTime{static_cast<std::uint32_t>(ms)};
}

}
