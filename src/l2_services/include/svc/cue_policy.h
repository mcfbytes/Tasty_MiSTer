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

    enum class Program : std::uint8_t {

        SentinelExclusive,

        SentinelPregapRelative,

        IndexTriple,

        IndexPair,
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

    enum class IsoSizing : std::uint8_t {

        ProbeVolumeDescriptor,

        Declared2048,
    };

    enum class FrameShaping : std::uint8_t { None, DescrambleByHeader, SealDataSector };

    enum class SubcodeShaping : std::uint8_t {
        None,
        Interleave,
    };

    TypeDetect type_detect = TypeDetect::Track0Only;
    OffsetOp offset_op = OffsetOp::Accumulate;
    SubNaming sub_naming = SubNaming::LastTrackFilename;
    bool wav_header_skip = false;

    bool sniff_sega_disc_system = false;

    Layout layout = Layout::Console;
    Program program = Program::SentinelExclusive;

    IsoSizing iso_sizing = IsoSizing::ProbeVolumeDescriptor;

    FrameShaping frame_shaping = FrameShaping::None;

    SubcodeShaping subcode_shaping = SubcodeShaping::None;
};

enum class CueAxis : std::uint32_t {
    None = 0,
    Layout = 1,
};

constexpr CueAxis unimplemented_axis(const CuePolicy& p) noexcept {
    if (p.layout != CuePolicy::Layout::Console)
        return CueAxis::Layout;
    return CueAxis::None;
}

constexpr bool requires_data_track(const CuePolicy& p) noexcept {
    return p.layout == CuePolicy::Layout::Dosbox;
}

}
