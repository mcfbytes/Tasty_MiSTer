// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/recorder_status.h"

#include "infra/json_out.h"

namespace mister::app {

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
        case RecVerdict::ScalerPortStuck:
            return "scaler_port_stuck";
        case RecVerdict::kCount:
            break;
    }
    return "?";
}

const char* rec_verdict_remedy(RecVerdict v) noexcept {
    if (v != RecVerdict::ScalerPortStuck) return nullptr;
    return "the scaler's memory port is stuck (its frames land early), so nothing can be "
           "recorded; reboot the MiSTer to clear it";
}

RecStatusText format_recorder_status(const RecorderStatus& s) noexcept {
    infra::JsonOut o;
    to_json(o, s);
    RecStatusText out;
    (void)out.assign(o.view());
    return out;
}

}  // namespace mister::app
