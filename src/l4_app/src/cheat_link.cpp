// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/cheat_link.h"

#include "cores/cheat_sink.h"
#include "cores/core_support.h"

namespace mister::app {

void CheatLink::content_loaded(cores::Core& core, const TxDigest& loaded) noexcept {
    TASTY_SEAT_BODY(CheatLink);
    if (auto r = core.cheat_sink().apply({}, cores::CheatUnit{}); !r) {
        ++refusals_;
    }
    blob_reader_.forget();
    digest_scratch_ = loaded;
    digest_cell_.publish(digest_scratch_);
}

bool CheatLink::apply_cheats_counted(const CheatCall& call) noexcept {
    TASTY_SEAT_BODY(CheatLink);
    if (!call.core) {
        ++refusals_;
        return false;
    }

    if (call.fio_held) {
        ++refusals_;
        return false;
    }
    if (!blob_reader_.take_if_changed(blob_, blob_scratch_)) {

        ++refusals_;
        return true;
    }
    cores::ICheatSink& sink = call.core->cheat_sink();

    if (auto r = sink.apply(blob_scratch_.body(), blob_scratch_.unit); !r) {
        ++refusals_;
        return false;
    }
    ++applies_;
    return true;
}

}
