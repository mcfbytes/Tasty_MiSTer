// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/selected.h"
#include "hal/spi_transport.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "proto/block_geometry.h"
#include "proto/block_slots.h"
#include "proto/types.h"

namespace mister::proto {

struct DiscAnnounce {
    TASTY_SEAT_RESIDENT(RT);

    [[nodiscard]] Ex<void> two_word_form(
        hal::ISpiTransport& link,
        SlotIndex slot,
        FileSize size_bytes
    ) const {
        {
            hal::Selected cs(link, hal::ChipSelect::Io);
            if (auto r = link.transfer(kSetSdInfo); !r)
                return std::unexpected(r.error());
            if (auto r = write32_word(link, static_cast<std::uint32_t>(size_bytes.v & 0xFFFFFFFFu));
                !r)
                return r;
            if (auto r = write32_word(link, static_cast<std::uint32_t>(size_bytes.v >> 32)); !r)
                return r;
        }
        if (slot.v > kMaxAnnounceableSlot) {
            return std::unexpected(Error{Errc::negotiation, ERR_SITE(), slot.v});
        }
        const auto parm = static_cast<std::uint16_t>((1u << slot.v) | 0x80u);
        hal::Selected cs(link, hal::ChipSelect::Io);
        if (auto r = link.transfer(kSetSdStat); !r)
            return std::unexpected(r.error());
        if (auto r = link.transfer(hal::SpiWord{parm}); !r)
            return std::unexpected(r.error());
        return {};
    }

    [[nodiscard]] Ex<void> flag_form(hal::ISpiTransport& link, bool loaded) const {
        {
            hal::Selected cs(link, hal::ChipSelect::Io);
            if (auto r = link.transfer(kSetSdInfo); !r)
                return std::unexpected(r.error());
            if (auto r = link.transfer(hal::SpiWord{static_cast<std::uint16_t>(loaded ? 1 : 0)});
                !r) {
                return std::unexpected(r.error());
            }
        }
        hal::Selected cs(link, hal::ChipSelect::Io);
        if (auto r = link.transfer(kSetSdStat); !r)
            return std::unexpected(r.error());
        if (auto r = link.transfer(hal::SpiWord{1}); !r)
            return std::unexpected(r.error());
        return {};
    }

    [[nodiscard]] Ex<void> mount_disc(
        BlockSlots& slots,
        hal::ISpiTransport& link,
        SlotIndex slot,
        FileSize size_bytes
    ) const {
        if (auto r = slots.install_disc(slot, size_bytes); !r)
            return r;
        auto r = two_word_form(link, slot, size_bytes);
        if (!r && r.error().code == Errc::negotiation && slot.v > kMaxAnnounceableSlot) {
            slots.count_unencodable_announce();
        }
        return r;
    }

    static constexpr hal::SpiWord kSetSdStat{0x1C};
    static constexpr hal::SpiWord kSetSdInfo{0x1D};

    [[nodiscard]] static Ex<void> write32_word(hal::ISpiTransport& link, std::uint32_t v) {
        if (auto r = link.transfer(hal::SpiWord{static_cast<std::uint16_t>(v & 0xFFFFu)}); !r) {
            return std::unexpected(r.error());
        }
        if (auto r = link.transfer(hal::SpiWord{static_cast<std::uint16_t>(v >> 16)}); !r) {
            return std::unexpected(r.error());
        }
        return {};
    }
};

}
