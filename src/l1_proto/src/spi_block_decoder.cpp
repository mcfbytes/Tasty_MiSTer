// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/spi_block_decoder.h"

namespace mister::proto {

void SpiBlockDecoder::service() {
    ++counters_.passes;
    for (unsigned i = 0; i < budget_; ++i) {
        auto d = slots_->block_poll().poll(*link_);
        if (!d) {
            ++counters_.errors;
            counters_.err_code = static_cast<std::uint16_t>(d.error().code);
            break;
        }
        if (!d->decoded) break;
        auto r = slots_->serve(*link_, *d);
        if (!r) {
            ++counters_.errors;
            counters_.err_code = static_cast<std::uint16_t>(r.error().code);
            break;
        }
        if (*r == BlockSlots::Pass::Deferred) {

            ++counters_.deferred;
            break;
        }
        const std::uint32_t bytes = d->req.block_count.v * d->block_size;

        (void)out_.push(
            LinkEvent::BlockRequest{.slot = d->req.slot, .lba = d->req.lba, .bytes = bytes});
        ++counters_.served;
        if (floor_.is_sector(bytes)) ++counters_.sectors;
    }
}

}  // namespace mister::proto
