// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hal/spi_transport.h"

namespace mister::hal {

class StartAckBound {
public:
    explicit StartAckBound(ISpiTransport& link) : link_(&link) { link_->widen_ack_bound(); }

    ~StartAckBound() {
        if (link_ != nullptr)
            link_->restore_ack_bound();
    }

    StartAckBound(const StartAckBound&) = delete;
    StartAckBound& operator=(const StartAckBound&) = delete;

    StartAckBound(StartAckBound&& o) noexcept : link_(o.link_) { o.link_ = nullptr; }

    StartAckBound& operator=(StartAckBound&&) = delete;

private:
    ISpiTransport* link_;
};

}
