// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"
#include "reactor/link_decoder.h"

namespace mister::proto {

template <class D>
class DecoderRow final : public reactor::ILinkDecoder {
    TASTY_SEAT_RESIDENT(RT);

public:
    explicit constexpr DecoderRow(D& d) noexcept : d_(&d) {}
    void service(reactor::CoreState&) const override {
        TASTY_SEAT_BODY(DecoderRow);
        d_->service();
    }
    constexpr bool active() const override {
        TASTY_SEAT_BODY(DecoderRow);
        return d_->active();
    }

private:
    D* d_;
};

}  // namespace mister::proto
