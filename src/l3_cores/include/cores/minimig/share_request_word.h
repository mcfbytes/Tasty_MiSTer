// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/mailbox_req_id.h"
#include "hal/reg_field.h"
#include "infra/endian.h"

namespace mister::cores::minimig {

struct ShareRequestWord {
    using Field = hal::RegField<std::uint32_t>;

    static constexpr Field kPattern{16, 0xFFFF};
    static constexpr Field kId{0, 0xFFFF};
    static constexpr Field kCheck{8, 0xFF};
    static constexpr Field kIdLow{0, 0xFF};
    static constexpr std::uint32_t kCheckBias = 77;
    static constexpr Field::Value kMagic{0x5AA5};

    std::uint32_t v = 0;

    [[nodiscard]] constexpr MailboxReqId req_id() const noexcept {
        return MailboxReqId{static_cast<std::uint16_t>(kId.get(v))};
    }

    constexpr void store_ack(std::span<std::byte, 2> dst) const noexcept {
        infra::store_le<std::uint16_t>(dst, static_cast<std::uint16_t>(kId.get(v)));
    }

    [[nodiscard]] constexpr bool whole() const noexcept {
        return Field::Value{kPattern.get(v)} == kMagic
            && kCheck.get(v) == kIdLow.get(v - kCheckBias);
    }

    friend constexpr bool operator==(ShareRequestWord, ShareRequestWord) = default;
};

static_assert(ShareRequestWord{0x5AA5'004Du}.whole(), "id 0x4D: check byte 0x00 = 0x4D - 77");
static_assert(!ShareRequestWord{0x5AA5'0001u}.whole() && !ShareRequestWord{0x1234'004Du}.whole());

}
