// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/spi_block_decoder.h"

#include "infra/prelude.h"

namespace mister::proto::link {

void SpiBlockDecoder::service(IBlockRoles& roles) {
    ++counters_.passes;
    for (unsigned i = 0; i < budget_; ++i) {
        auto d = slots_.poll_block(roles, std::nullopt);
        if (!d) {
            ++counters_.errors;
            counters_.err_code = errc_code(d.error().code);
            break;
        }
        if (!d->decoded) {

            if (route_ == BlockPollRoute::UntilIdle && life_ == PollLife::Polling) {
                life_ = PollLife::Retired;
                ++counters_.idle_retirements;
            }
            break;
        }
        auto r = slots_.serve(*d, roles);
        if (!r) {
            ++counters_.errors;
            counters_.err_code = errc_code(r.error().code);
            break;
        }
        if (*r == BlockSlots::Pass::Deferred) {

            ++counters_.deferred;
            break;
        }
        const std::uint32_t bytes = d->block_size.window_bytes(d->req.block_count);

        (void)out_.push(
            LinkEvent::BlockRequest{.slot = d->req.slot, .lba = d->req.lba, .bytes = bytes}
        );
        ++counters_.served;
        if (floor_.is_sector(bytes))
            ++counters_.sectors;
    }
}

}
