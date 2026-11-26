// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/ocram_mailbox_slot.h"
#include "hal/reg_field.h"

namespace mister::hal::ocram {

struct BindSet;
class RecordHeader;
class SectorAsk;

class SlotBinds {
public:
    constexpr SlotBinds() noexcept = default;

    [[nodiscard]] constexpr bool has(MailboxSlot k) const noexcept { return (v & bit_(k)) != 0u; }

    [[nodiscard]] constexpr SlotBinds with(MailboxSlot k) const noexcept {
        return SlotBinds{v | bit_(k)};
    }

    [[nodiscard]] constexpr SlotBinds without(MailboxSlot k) const noexcept {
        return SlotBinds{v & ~bit_(k)};
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0u; }

    [[nodiscard]] constexpr SlotBinds common(SlotBinds o) const noexcept {
        return SlotBinds{v & o.v};
    }

    friend constexpr bool operator==(SlotBinds, SlotBinds) = default;

private:
    friend struct BindSet;
    friend class RecordHeader;
    friend class SectorAsk;

    static constexpr RegField<std::uint32_t> kByte{0, 0xFF};

    constexpr explicit SlotBinds(std::uint32_t byte) noexcept : v(kByte.get(byte)) {}

    [[nodiscard]] constexpr RegField<std::uint32_t>::Value byte() const noexcept {
        return RegField<std::uint32_t>::Value{v};
    }

    [[nodiscard]] static constexpr std::uint32_t bit_(MailboxSlot k) noexcept {
        switch (k) {
            case MailboxSlot::Sector:
                return 1u << 0;
            case MailboxSlot::Mount0:
                return 1u << 1;
            case MailboxSlot::Mount1:
                return 1u << 2;
        }
        return 0u;
    }

    std::uint32_t v = 0;
};

}
