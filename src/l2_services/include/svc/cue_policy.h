// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"

namespace mister::svc {

struct CuePolicy {
    TASTY_SEAT_EXEMPT(const_shared);

    enum class Layout : std::uint8_t {
        Console,
        Dosbox,

    };

    enum class TypeDetect : std::uint8_t {
        Track0Only,
        PerTrack,

        PerTrackRunningSize,
    };
    enum class OffsetOp : std::uint8_t {
        Accumulate,

        Assign,
    };
    enum class SubNaming : std::uint8_t {
        LastTrackFilename,
        ImageFilename,
        None,

        ImageSubOrCdg,
    };

    enum class TrackClose : std::uint8_t {
        SentinelExclusive,

        ProvisionalInclusive,

        SentinelPregapRelative,
    };

    enum class PregapModel : std::uint8_t {
        SingleAccumulator,

        IndexTriple,

        IndexPair,
    };

    enum class IsoSizing : std::uint8_t {

        ProbeVolumeDescriptor,

        Declared2048,
    };

    enum class PostLoadTocShift : std::uint8_t {
        None,

        FakeLeadInPregap150,

        Uniform150,
    };

    enum class FrameShaping : std::uint8_t { None, DescrambleByHeader };

    TypeDetect type_detect = TypeDetect::Track0Only;
    OffsetOp offset_op = OffsetOp::Accumulate;
    SubNaming sub_naming = SubNaming::LastTrackFilename;
    bool wav_header_skip = false;

    bool sniff_sega_disc_system = false;

    Layout layout = Layout::Console;
    TrackClose track_close = TrackClose::SentinelExclusive;
    PregapModel pregap_model = PregapModel::SingleAccumulator;
    PostLoadTocShift post_load_shift = PostLoadTocShift::None;

    IsoSizing iso_sizing = IsoSizing::ProbeVolumeDescriptor;

    FrameShaping frame_shaping = FrameShaping::None;
};

enum class CueAxis : std::uint32_t {
    None = 0,
    Layout = 1,
    TrackClose = 2,
    PregapModel = 3,
    PostLoadShift = 4,
};

constexpr CueAxis unimplemented_axis(const CuePolicy& p) noexcept {
    if (p.layout != CuePolicy::Layout::Console) return CueAxis::Layout;

    if (p.track_close == CuePolicy::TrackClose::ProvisionalInclusive &&
        p.pregap_model == CuePolicy::PregapModel::IndexTriple &&
        p.post_load_shift == CuePolicy::PostLoadTocShift::FakeLeadInPregap150) {
        return CueAxis::None;
    }

    if (p.track_close == CuePolicy::TrackClose::ProvisionalInclusive &&
        p.pregap_model == CuePolicy::PregapModel::IndexPair &&
        p.post_load_shift == CuePolicy::PostLoadTocShift::Uniform150) {
        return CueAxis::None;
    }

    if (p.track_close != CuePolicy::TrackClose::SentinelExclusive &&
        p.track_close != CuePolicy::TrackClose::SentinelPregapRelative) {
        return CueAxis::TrackClose;
    }
    if (p.pregap_model != CuePolicy::PregapModel::SingleAccumulator) {
        return CueAxis::PregapModel;
    }
    if (p.post_load_shift != CuePolicy::PostLoadTocShift::None) return CueAxis::PostLoadShift;
    return CueAxis::None;
}

constexpr bool requires_data_track(const CuePolicy& p) noexcept {
    return p.layout == CuePolicy::Layout::Dosbox;
}

}  // namespace mister::svc
