// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/core_block_roles.h"
#include "cores/core_support.h"
#include "infra/seat.h"
#include "proto/no_block_roles.h"
#include "proto/spi_block_decoder.h"
#include "reactor/link_decoder.h"

namespace mister::app {

class CoreBlockRow final : public reactor::ILinkDecoder {
    TASTY_SEAT_RESIDENT(RT);

public:
    explicit constexpr CoreBlockRow(proto::SpiBlockDecoder& decoder) noexcept : d_(&decoder) {}

    void service(reactor::CoreState& st) const override {
        TASTY_SEAT_BODY(CoreBlockRow);
        if (cores::Core* core = cores::bound_core(st)) {
            CoreBlockRoles roles{*core};
            d_->service(roles);
        } else {
            proto::NoBlockRoles none;
            d_->service(none);
        }
    }

    constexpr bool active() const override {
        TASTY_SEAT_BODY(CoreBlockRow);
        return d_->active();
    }

    std::uint32_t claim_count() const override {
        TASTY_SEAT_BODY(CoreBlockRow);
        return d_->osd_claims();
    }

private:
    proto::SpiBlockDecoder* d_;
};

}
