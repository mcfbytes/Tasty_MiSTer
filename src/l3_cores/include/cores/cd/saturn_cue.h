// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "svc/disc/cue_policy.h"

namespace mister::cores::cd::saturn {

inline constexpr svc::disc::CuePolicy kSaturnCuePolicy{

    .type_detect = svc::disc::CuePolicy::TypeDetect::PerTrackRunningSize,
    .offset_op = svc::disc::CuePolicy::OffsetOp::Assign,
    .sub_naming = svc::disc::CuePolicy::SubNaming::None,
    .wav_header_skip = false,
    .sniff_sega_disc_system = false,
    .layout = svc::disc::CuePolicy::Layout::Console,

    .program = svc::disc::CuePolicy::Program::SentinelPregapRelative,
    .iso_sizing = svc::disc::CuePolicy::IsoSizing::ProbeVolumeDescriptor,

    .frame_shaping = svc::disc::CuePolicy::FrameShaping::SealDataSector,
};

static_assert(
    svc::disc::unimplemented_axis(kSaturnCuePolicy) == svc::disc::CueAxis::None,
    "Saturn's CUE column must name a program DiscEngine already has"
);

}
