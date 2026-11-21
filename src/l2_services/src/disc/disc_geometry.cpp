// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/disc/disc_geometry.h"

#include <cstddef>
#include <iterator>

namespace mister::svc::disc {

namespace {

struct SectorGroup {
    proto::BlockCount per_rev;
    Lba start;
    Lba end;
    float rotation_ms;
};

constexpr SectorGroup kSectorGroups[] = {
    {proto::BlockCount{10}, Lba{0}, Lba{12572}, 133.47f},
    {proto::BlockCount{11}, Lba{12573}, Lba{30244}, 146.82f},
    {proto::BlockCount{12}, Lba{30245}, Lba{49523}, 160.17f},
    {proto::BlockCount{13}, Lba{49524}, Lba{70408}, 173.51f},
    {proto::BlockCount{14}, Lba{70409}, Lba{92900}, 186.86f},
    {proto::BlockCount{15}, Lba{92901}, Lba{116998}, 200.21f},
    {proto::BlockCount{16}, Lba{116999}, Lba{142703}, 213.56f},
    {proto::BlockCount{17}, Lba{142704}, Lba{170014}, 226.90f},
    {proto::BlockCount{18}, Lba{170015}, Lba{198932}, 240.25f},
    {proto::BlockCount{19}, Lba{198933}, Lba{229456}, 253.60f},
    {proto::BlockCount{20}, Lba{229457}, Lba{261587}, 266.95f},
    {proto::BlockCount{21}, Lba{261588}, Lba{295324}, 280.29f},
    {proto::BlockCount{22}, Lba{295325}, Lba{330668}, 293.64f},
    {proto::BlockCount{23}, Lba{330669}, Lba{333012}, 306.99f},
};
constexpr std::size_t kNumSectorGroups = std::size(kSectorGroups);

std::size_t find_group(Lba sector) noexcept {
    for (std::size_t i = 0; i < kNumSectorGroups; ++i) {
        if (sector >= kSectorGroups[i].start && sector <= kSectorGroups[i].end)
            return i;
    }
    return 0;
}

float turns(Lba a, Lba b, proto::BlockCount per) noexcept {
    return a <= b ? (b - a).whole_turns(per) : -(a - b).whole_turns(per);
}

}

std::optional<TrackIndex> track_for_lba(const Toc& toc, Lba lba) noexcept {

    for (TrackIndex i{}; toc.last.contains(i); i = i + TrackCount{1}) {
        if (lba < toc.tracks[i].end)
            return i;
    }
    return std::nullopt;
}

SeekTime seek_time(SeekSpan span) noexcept {

    constexpr std::uint32_t kMaxSector = 360000;
    constexpr double kTracksPerGroupGap = 1606.48;
    constexpr proto::BlockCount kSettleSectors{3};
    constexpr proto::BlockCount kShortJumpSectors{7};
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
    if (!span.within(Lba{kMaxSector}))
        return SeekTime{0};

    const std::size_t si = find_group(span.from);
    const std::size_t ti = find_group(span.to);

    float track_difference = 0.0f;
    if (ti == si) {
        track_difference = span.distance().whole_turns(kSectorGroups[ti].per_rev);
    } else if (ti > si) {
        track_difference = turns(span.from, kSectorGroups[si].end, kSectorGroups[si].per_rev);
        track_difference += turns(kSectorGroups[ti].start, span.to, kSectorGroups[ti].per_rev);
        track_difference = static_cast<float>(
            static_cast<double>(track_difference)
            + kTracksPerGroupGap * static_cast<double>(ti - si - 1)
        );
    } else {
        track_difference = turns(kSectorGroups[si].start, span.from, kSectorGroups[si].per_rev);
        track_difference += turns(span.to, kSectorGroups[ti].end, kSectorGroups[ti].per_rev);
        track_difference = static_cast<float>(
            static_cast<double>(track_difference)
            + kTracksPerGroupGap * static_cast<double>(si - ti - 1)
        );
    }

    const proto::BlockCount delta = span.distance();
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
