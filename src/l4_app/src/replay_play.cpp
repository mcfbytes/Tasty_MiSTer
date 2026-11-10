// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/replay_play.h"

#include "infra/diag_log.h"

namespace mister::app {

void log_end(xthread::DiagLog& diag, const ReplayStatus& s, const ReplayPlay::Run& run) noexcept {
    diag.appendf(
        "{\"t\":\"tas\",\"k\":\"end\",%s,\"tries\":%u}",
        format_replay_status(s).c_str(),
        run.tries
    );
}

}
