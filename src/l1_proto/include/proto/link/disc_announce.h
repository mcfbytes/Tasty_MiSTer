// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/types.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/block_geometry.h"
#include "proto/link/block_slots.h"
#include "proto/link_frame.h"
#include "proto/types.h"
#include "proto/uio_opcodes.h"

namespace mister::proto::link {

struct DiscAnnounce {
    TASTY_SEAT_RESIDENT(RT);

    static constexpr hal::SpiWord kLoadedFlag{1};
    static constexpr hal::SpiWord kUnloadedFlag{0};

    [[nodiscard]] Ex<void> two_word_form(
        ILinkFrame& frame,
        SlotIndex slot,
        FileSize size_bytes
    ) const {
        if (auto r = frame.begin(UioOp::SetSdInfo); !r)
            return r;
        if (auto r = size_bytes.emit_words(frame, hal::Width::Word); !r)
            return r;
        if (auto r = frame.end(); !r)
            return r;
        if (slot > kLastAnnounceableSlot) {
            return std::unexpected(slot.refusal(Errc::negotiation, ERR_SITE()));
        }

        const SdStatParameter parm = SdStatParameter::of(slot, false);
        if (auto r = frame.begin(UioOp::SetSdStat); !r)
            return r;
        if (auto r = frame.word(parm.wire_word()); !r)
            return r;
        return frame.end();
    }

    [[nodiscard]] Ex<void> flag_form(ILinkFrame& frame, bool loaded) const {
        if (auto r = frame.begin(UioOp::SetSdInfo); !r)
            return r;
        if (auto r = frame.word(loaded ? kLoadedFlag : kUnloadedFlag); !r)
            return r;
        if (auto r = frame.end(); !r)
            return r;
        if (auto r = frame.begin(UioOp::SetSdStat); !r)
            return r;
        if (auto r = frame.word(hal::SpiWord{1}); !r)
            return r;
        return frame.end();
    }

    [[nodiscard]] Ex<void> mount_disc(
        BlockSlots& slots,
        ILinkFrame& frame,
        SlotIndex slot,
        FileSize size_bytes,
        IBlockRoles& roles
    ) const {
        if (auto r = slots.install_disc(slot, size_bytes, roles); !r)
            return r;
        auto r = two_word_form(frame, slot, size_bytes);
        if (!r && r.error().code == Errc::negotiation && slot > kLastAnnounceableSlot) {
            slots.count_unencodable_announce();
        }
        return r;
    }
};

}
