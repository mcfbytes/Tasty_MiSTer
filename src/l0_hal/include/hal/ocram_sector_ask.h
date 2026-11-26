// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/ocram_mailbox_slot.h"
#include "hal/ocram_slot_binds.h"

namespace mister::hal::ocram {

template <class Owner>
class RingWriter;

class SectorAsk {
public:
    constexpr SectorAsk() noexcept = default;

    [[nodiscard]] static constexpr SectorAsk asking(bool on) noexcept {
        return on ? SectorAsk{}.with(MailboxSlot::Sector) : SectorAsk{};
    }

    [[nodiscard]] constexpr bool asks() const noexcept { return asks(MailboxSlot::Sector); }

    [[nodiscard]] constexpr bool asks(MailboxSlot k) const noexcept {
        return (v & SlotBinds::bit_(k)) != 0u;
    }

    [[nodiscard]] constexpr SectorAsk with(MailboxSlot k) const noexcept {
        return SectorAsk{v | SlotBinds::bit_(k)};
    }

    [[nodiscard]] constexpr SectorAsk without(MailboxSlot k) const noexcept {
        return SectorAsk{v & ~SlotBinds::bit_(k)};
    }

    [[nodiscard]] constexpr SlotBinds slots() const noexcept { return SlotBinds{v}; }

    friend constexpr bool operator==(SectorAsk, SectorAsk) = default;

private:
    template <class Owner>
    friend class RingWriter;

    constexpr explicit SectorAsk(std::uint32_t word) noexcept : v(word) {}

    [[nodiscard]] constexpr std::uint32_t word() const noexcept { return v; }

    std::uint32_t v = 0;
};

static_assert(
    SectorAsk::asking(true).asks(MailboxSlot::Sector)
        && !SectorAsk::asking(true).asks(MailboxSlot::Mount0),
    "the SECTOR slot's ask is H_SECTOR bit 0 alone"
);

}
