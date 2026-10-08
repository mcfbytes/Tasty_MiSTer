// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/replay_status.h"

#include "infra/json_out.h"

namespace mister::app {

const char* replay_end_sentence(ReplayEnd e, EpochFail fail) noexcept {
    switch (e) {
        case ReplayEnd::Superseded:
            return "a newer replay replaced this one; start the movie again if you still want it";
        case ReplayEnd::CoreSwitch:
            return "the core changed during the movie; play it again";
        case ReplayEnd::RefLost:
            return "tasty lost track of the core's frames during the movie; play it again";
        case ReplayEnd::NoReference:
            return "tasty cannot see this core's frames, so it cannot time a movie on it; use the "
                   "system's standard core";
        case ReplayEnd::EpochAmbiguous:
            if (fail == EpochFail::Untimed)
                return "the core's first frame came too late to time a negative --lead; pass "
                       "--lead 0 or higher, or play it again";
            return "the movie's first frame could not be placed on the core's frames, even after "
                   "reloading; play it again";
        case ReplayEnd::Refused:
            return "the replay was refused before it started; check the line above and try again";
        case ReplayEnd::None:
        case ReplayEnd::Finished:
        case ReplayEnd::Stopped:
        case ReplayEnd::kCount:
            return "the replay stopped; play it again";
    }

    return "the replay stopped; play it again";
}

ReplayStatusText format_replay_status(const ReplayStatus& s) noexcept {
    infra::JsonOut o;
    to_json(o, s);
    ReplayStatusText out{};
    (void)out.assign(o.view());
    return out;
}

}  // namespace mister::app
