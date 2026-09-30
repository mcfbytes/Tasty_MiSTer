// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/spi_identify_decoder.h"

namespace mister::proto {

void SpiIdentifyDecoder::service() noexcept {
    const Ex<hal::CoreIdentity> id = signals_->identify();
    if (session_->phase() == SessionPhase::Start) {
        if (const auto t = session_->accept_identity(id); !t) {
            refuse_(t.error(), true);
            return;
        }
        if (const auto r = session_->configure_after_identity(); !r) {
            refuse_(r.error(), true);
            return;
        }
    } else if (!id) {
        refuse_(id.error(), false);
        return;
    }
    (void)out_.push(
        LinkEvent::IdentityMatched{.dual_sdram = id->dual_sdram,
                                   .type_byte = id->type_byte,
                                   .bind_gen = *gen_,
                                   .wide = session_->capabilities().width == hal::Width::Word});
}

void SpiIdentifyDecoder::refuse_(const Error& e, bool at_accept) noexcept {
    (void)out_.push(LinkEvent::IdentityMismatch{
        .bind_gen = *gen_, .err = e.code, .raw_word = e.detail, .at_accept = at_accept});
}

}  // namespace mister::proto
