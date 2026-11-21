// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/spi_identify_decoder.h"

#include "infra/prelude.h"

namespace mister::proto::link {

void SpiIdentifyDecoder::service() noexcept {

    if (session_.configure_posted()) {
        const auto settled = session_.settle_configure();
        if (!settled) {
            refuse_(settled.error(), true);
            return;
        }
        if (!*settled)
            return;
    }
    const Ex<hal::CoreTypeByte> id = signals_.identify();
    if (session_.phase() == SessionPhase::Start) {
        if (const auto t = session_.accept_identity(id); !t) {
            refuse_(t.error(), true);
            return;
        }
        if (const auto r = session_.configure_after_identity(); !r) {
            refuse_(r.error(), true);
            return;
        }

        if (session_.configure_posted())
            return;
    } else if (!id) {
        refuse_(id.error(), false);
        return;
    }
    (void)out_.push(
        LinkEvent::IdentityMatched{
            .dual_sdram = id->dual_sdram(),
            .type_byte = *id,
            .bind_gen = gen_,
            .width = session_.capabilities().width,
            .lw = session_.capabilities().lw
        }
    );
}

void SpiIdentifyDecoder::refuse_(const Error& e, bool at_accept) noexcept {
    (void)out_.push(
        LinkEvent::IdentityMismatch{
            .bind_gen = gen_,
            .err = e.code,
            .raw_word = e.detail,
            .at_accept = at_accept
        }
    );
}

}
