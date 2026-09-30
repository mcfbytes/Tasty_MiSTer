// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hal/spi_transport.h"

namespace mister::hal {

class Selected {
public:
    Selected(ISpiTransport& link, ChipSelect cs) : link_(&link) { link_->select(cs); }
    ~Selected() {
        if (link_ != nullptr) link_->deselect();
    }
    Selected(const Selected&) = delete;
    Selected& operator=(const Selected&) = delete;
    Selected(Selected&& o) noexcept : link_(o.link_) { o.link_ = nullptr; }
    Selected& operator=(Selected&&) = delete;

    void release() noexcept {
        if (link_ != nullptr) {
            link_->deselect();
            link_ = nullptr;
        }
    }

private:
    ISpiTransport* link_;
};

}  // namespace mister::hal
