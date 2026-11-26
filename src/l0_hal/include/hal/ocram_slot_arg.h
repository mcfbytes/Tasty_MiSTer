// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "hal/ocram_mailbox_slot.h"
#include "hal/reg_field.h"

namespace mister::hal::ocram {

class RecordHeader;

class SlotArg {
public:
    constexpr SlotArg() noexcept = default;

    [[nodiscard]] static constexpr SlotArg primary() noexcept { return SlotArg{}; }

    [[nodiscard]] static constexpr SlotArg of(MailboxSlot k) noexcept {
        switch (k) {
            case MailboxSlot::Sector:
                return SlotArg{kSector};
            case MailboxSlot::Mount0:
                return SlotArg{kMount0};
            case MailboxSlot::Mount1:
                return SlotArg{kMount1};
        }
        return SlotArg{};
    }

    [[nodiscard]] constexpr std::optional<MailboxSlot> slot() const noexcept {
        switch (kSlot.get(v)) {
            case kSector:
                return MailboxSlot::Sector;
            case kMount0:
                return MailboxSlot::Mount0;
            case kMount1:
                return MailboxSlot::Mount1;
            default:
                return std::nullopt;
        }
    }

    [[nodiscard]] constexpr bool one_block() const noexcept { return kBlkCnt.get(v) == 0u; }

    friend constexpr bool operator==(SlotArg, SlotArg) = default;

private:
    friend class RecordHeader;
    static constexpr RegField<std::uint8_t> kSlot{0, 0x3};
    static constexpr RegField<std::uint8_t> kBlkCnt{2, 0x3F};
    static constexpr std::uint8_t kSector = 0;
    static constexpr std::uint8_t kMount0 = 1;
    static constexpr std::uint8_t kMount1 = 2;

    constexpr explicit SlotArg(std::uint8_t field) noexcept : v(field) {}

    [[nodiscard]] constexpr RegField<std::uint32_t>::Value field() const noexcept {
        return RegField<std::uint32_t>::Value{v};
    }

    [[nodiscard]] static constexpr SlotArg of_field(std::uint32_t field) noexcept {
        return SlotArg{static_cast<std::uint8_t>(field)};
    }

    std::uint8_t v = 0;
};

static_assert(SlotArg::of(MailboxSlot::Sector) == SlotArg::primary(), "k = 0's arg is 0");
static_assert(SlotArg::of(MailboxSlot::Mount1).slot() == MailboxSlot::Mount1);

}
