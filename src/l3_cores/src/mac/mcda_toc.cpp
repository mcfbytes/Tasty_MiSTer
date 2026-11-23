// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/mac/mcda_toc.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace mister::cores::mac {

namespace {

constexpr std::size_t kHeaderBytes = 16;
constexpr std::size_t kTrackBytes = 8;
static_assert(kHeaderBytes + kMcdaTocTracks * kTrackBytes <= kMcdaTocBytes);
constexpr std::array<std::byte, 4>
    kMagic{std::byte{'M'}, std::byte{'C'}, std::byte{'D'}, std::byte{'A'}};
constexpr std::byte kVersion{1};
constexpr std::byte kAudioControl{0x10};
constexpr std::byte kDataControl{0x14};

enum HeaderAt : std::size_t {
    kVersionAt = 4,
    kFirstTrackAt = 5,
    kTrackCountAt = 6,
    kDataTrackAt = 7,
    kLeadOutAt = 8,
};

enum EntryAt : std::size_t { kControlAt = 0, kStartAt = 2, kPregapAt = 6 };

}

std::optional<svc::TrackIndex> first_data_track(const svc::disc::Toc& toc) noexcept {
    std::optional<svc::TrackIndex> found;
    toc.last.for_each_index([&](svc::TrackIndex i) {
        if (!found && toc.tracks[i].type != svc::disc::TrackType::Cdda)
            found = i;
    });
    return found;
}

std::array<std::byte, kMcdaTocBytes> mcda_toc(
    const svc::disc::Toc& toc,
    std::optional<svc::TrackIndex> data_track
) noexcept {
    std::array<std::byte, kMcdaTocBytes> b{};
    const std::span<std::byte, kHeaderBytes> head = std::span(b).first<kHeaderBytes>();
    std::ranges::copy(kMagic, head.begin());
    head[kVersionAt] = kVersion;
    svc::TrackIndex{}.number().store_byte(head.subspan<kFirstTrackAt, 1>());
    toc.last.store_byte(head.subspan<kTrackCountAt, 1>());

    if (data_track)
        data_track->number().store_byte(head.subspan<kDataTrackAt, 1>());
    toc.end.store_le32(head.subspan<kLeadOutAt, 4>());
    std::size_t listed = 0;
    toc.last.for_each_index([&](svc::TrackIndex i) {
        if (listed >= kMcdaTocTracks)
            return;
        const svc::disc::Track& t = toc.tracks[i];
        const std::span<std::byte, kTrackBytes> e(
            b.data() + kHeaderBytes + kTrackBytes * listed,
            kTrackBytes
        );
        e[kControlAt] = t.type == svc::disc::TrackType::Cdda ? kAudioControl : kDataControl;
        t.start.store_le32(e.subspan<kStartAt, 4>());
        t.pregap.store_le16_saturated(e.subspan<kPregapAt, 2>());
        ++listed;
    });
    return b;
}

}
