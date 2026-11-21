// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/status_decoder.h"

#include <optional>

namespace mister::proto::link {

void StatusDecoder::service() noexcept {
    TASTY_SEAT_BODY(StatusDecoder);

    if (!session_.flush_owed()) {
        ++flush_refusals_;
        return;
    }
    const std::optional<StatusChange> change = source_.get().take(last_, words_);
    if (!change)
        return;
    last_ = *change;
    if (!session_.adopt_from_core(words_))
        ++flush_refusals_;
    published_ = session_.status().value();
    cell_.publish(published_);
}

}
