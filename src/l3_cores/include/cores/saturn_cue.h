// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "svc/cue_policy.h"

namespace mister::cores::saturn {

inline constexpr svc::CuePolicy kSaturnCuePolicy{

    .type_detect = svc::CuePolicy::TypeDetect::PerTrackRunningSize,
    .offset_op = svc::CuePolicy::OffsetOp::Assign,
    .sub_naming = svc::CuePolicy::SubNaming::None,
    .wav_header_skip = false,
    .sniff_sega_disc_system = false,
    .layout = svc::CuePolicy::Layout::Console,

    .track_close = svc::CuePolicy::TrackClose::SentinelPregapRelative,
    .pregap_model = svc::CuePolicy::PregapModel::SingleAccumulator,
    .post_load_shift = svc::CuePolicy::PostLoadTocShift::None,
    .iso_sizing = svc::CuePolicy::IsoSizing::ProbeVolumeDescriptor,
};

static_assert(
    svc::unimplemented_axis(kSaturnCuePolicy) == svc::CueAxis::None,
    "Saturn's CUE column must name a program DiscEngine already has"
);

}
