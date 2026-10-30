// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "infra/seat.h"
#include "proto/block_geometry.h"
#include "proto/block_slots.h"
#include "proto/link_frame.h"
#include "proto/types.h"
#include "proto/uio_opcodes.h"

namespace mister::proto {

struct DiscAnnounce {
    TASTY_SEAT_RESIDENT(RT);

    [[nodiscard]] Ex<void> two_word_form(
        ILinkFrame& frame,
        SlotIndex slot,
        FileSize size_bytes
    ) const {
        if (auto r = frame.begin(FrameSel::Uio, static_cast<std::uint16_t>(UioOp::SetSdInfo)); !r)
            return r;
        if (auto r = write32_word(frame, static_cast<std::uint32_t>(size_bytes.v & 0xFFFFFFFFu));
            !r)
            return r;
        if (auto r = write32_word(frame, static_cast<std::uint32_t>(size_bytes.v >> 32)); !r)
            return r;
        if (auto r = frame.end(); !r)
            return r;
        if (slot.v > kMaxAnnounceableSlot) {
            return std::unexpected(Error{Errc::negotiation, ERR_SITE(), slot.v});
        }
        const auto parm = static_cast<std::uint16_t>((1u << slot.v) | 0x80u);
        if (auto r = frame.begin(FrameSel::Uio, static_cast<std::uint16_t>(UioOp::SetSdStat)); !r)
            return r;
        if (auto r = frame.word(parm); !r)
            return r;
        return frame.end();
    }

    [[nodiscard]] Ex<void> flag_form(ILinkFrame& frame, bool loaded) const {
        if (auto r = frame.begin(FrameSel::Uio, static_cast<std::uint16_t>(UioOp::SetSdInfo)); !r)
            return r;
        if (auto r = frame.word(static_cast<std::uint16_t>(loaded ? 1 : 0)); !r)
            return r;
        if (auto r = frame.end(); !r)
            return r;
        if (auto r = frame.begin(FrameSel::Uio, static_cast<std::uint16_t>(UioOp::SetSdStat)); !r)
            return r;
        if (auto r = frame.word(1); !r)
            return r;
        return frame.end();
    }

    [[nodiscard]] Ex<void> mount_disc(
        BlockSlots& slots,
        ILinkFrame& frame,
        SlotIndex slot,
        FileSize size_bytes
    ) const {
        if (auto r = slots.install_disc(slot, size_bytes); !r)
            return r;
        auto r = two_word_form(frame, slot, size_bytes);
        if (!r && r.error().code == Errc::negotiation && slot.v > kMaxAnnounceableSlot) {
            slots.count_unencodable_announce();
        }
        return r;
    }

    [[nodiscard]] static Ex<void> write32_word(ILinkFrame& frame, std::uint32_t v) {
        if (auto r = frame.word(static_cast<std::uint16_t>(v & 0xFFFFu)); !r)
            return r;
        return frame.word(static_cast<std::uint16_t>(v >> 16));
    }
};

}
