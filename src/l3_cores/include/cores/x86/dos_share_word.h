// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/mailbox_req_id.h"
#include "hal/reg_field.h"
#include "infra/endian.h"

namespace mister::cores::x86 {

struct DosShareWord {
    using Field = hal::RegField<std::uint32_t>;

    static constexpr Field kMagic{16, 0xFFFF};
    static constexpr Field kId{0, 0xFFFF};
    static constexpr Field kCheck{8, 0xFF};
    static constexpr Field kIdLow{0, 0xFF};
    static constexpr std::uint32_t kCheckBias = 77;
    static constexpr Field::Value kMagicValue{0xA55A};

    std::uint32_t v = 0;

    [[nodiscard]] static constexpr DosShareWord load(std::span<const std::byte, 4> bytes) noexcept {
        return DosShareWord{infra::load_le<std::uint32_t>(bytes)};
    }

    [[nodiscard]] constexpr MailboxReqId req_id() const noexcept {
        return MailboxReqId{static_cast<std::uint16_t>(kId.get(v))};
    }

    constexpr void store_ack(std::span<std::byte, 2> dst) const noexcept {
        infra::store_le<std::uint16_t>(dst, static_cast<std::uint16_t>(kId.get(v)));
    }

    [[nodiscard]] constexpr bool whole() const noexcept {
        return Field::Value{kMagic.get(v)} == kMagicValue
            && kCheck.get(v) == kIdLow.get(v + kCheckBias);
    }

    friend constexpr bool operator==(DosShareWord, DosShareWord) = default;
};

static_assert(DosShareWord{0xA55A'9A4Du}.whole(), "id 0x9A4D: check 0x9A = low byte 0x4D + 77");
static_assert(!DosShareWord{0xA55A'0001u}.whole() && !DosShareWord{0x1234'0001u}.whole());

}
