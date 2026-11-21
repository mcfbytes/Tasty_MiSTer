// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/input/pad_egress.h"

#include "app/input/lw_pad_endpoint.h"

namespace mister::app::input {

infra::Tally PadEgress::binds() const noexcept {
    return lw_ ? lw_->bind_count() : infra::Tally{};
}

IPadLevelSink& PadEgress::route_() noexcept {
    if (lw_ && lw_->bound())
        return *lw_;
    return link_;
}

}
