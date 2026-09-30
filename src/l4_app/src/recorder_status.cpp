// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/recorder_status.h"

#include <cinttypes>
#include <cstdio>

namespace mister::app {

const char* rec_state_name(RecState s) noexcept {
    switch (s) {
        case RecState::Idle:
            return "idle";
        case RecState::Probing:
            return "probing";
        case RecState::Armed:
            return "armed";
        case RecState::Recording:
            return "recording";
        case RecState::Closing:
            return "closing";
    }
    return "?";
}

const char* rec_verdict_name(RecVerdict v) noexcept {
    switch (v) {
        case RecVerdict::None:
            return "none";
        case RecVerdict::Started:
            return "started";
        case RecVerdict::Stopped:
            return "stopped";
        case RecVerdict::NoWindow:
            return "no_window";
        case RecVerdict::NoLiveBuffer:
            return "no_live_buffer";
        case RecVerdict::Busy:
            return "busy";
        case RecVerdict::WriterFailed:
            return "writer_failed";
        case RecVerdict::CoreSwitch:
            return "core_switch";
        case RecVerdict::ReplayEnded:
            return "replay_ended";
        case RecVerdict::NoCodec:
            return "no_codec";
        case RecVerdict::NoMemory:
            return "no_memory";
        case RecVerdict::kCount:
            break;
    }
    return "?";
}

RecStatusText format_recorder_status(const RecorderStatus& s) noexcept {
    char buf[RecStatusText::kBufSize];
    (void)std::snprintf(
        buf, sizeof buf,
        "\"state\":\"%s\",\"gen\":%u,\"avi\":%u,\"stride_mib\":%u,\"lowlat\":%u,\"anchored\":%u,"
        "\"interlaced\":%u,"
        "\"w\":%u,"
        "\"h\":%u,\"rows\":%" PRIu32 ",\"captured\":%" PRIu32 ",\"missed\":%" PRIu32
        ",\"torn\":%" PRIu32 ",\"backpressure\":%" PRIu32 ",\"resize\":%" PRIu32
        ",\"gaps\":%" PRIu32 ",\"drift\":%" PRIu32 ",\"rebased\":%" PRIu32
        ",\"bad_header\":%" PRIu32 ",\"woven\":%" PRIu32 ",\"unmatched\":%" PRIu32
        ",\"copy_us\":%" PRIu32 ",\"copy_us_max\":%" PRIu32 ",\"arena_kib\":%" PRIu32
        ",\"first\":%" PRIu64 ",\"last\":%" PRIu64,
        rec_state_name(s.state), static_cast<unsigned>(s.gen), static_cast<unsigned>(s.avi),
        static_cast<unsigned>(s.stride_mib), static_cast<unsigned>(s.lowlat),
        static_cast<unsigned>(s.anchored), static_cast<unsigned>(s.interlaced),
        static_cast<unsigned>(s.width), static_cast<unsigned>(s.height), s.rows, s.captured,
        s.missed, s.torn, s.backpressure, s.resize, s.gap_estimated, s.stamp_drift, s.rebased,
        s.bad_header, s.woven, s.unmatched, s.copy_us_last, s.copy_us_max, s.arena_kib,
        s.first_core_frame, s.last_core_frame);
    RecStatusText out;
    (void)out.assign(buf);
    return out;
}

}  // namespace mister::app
