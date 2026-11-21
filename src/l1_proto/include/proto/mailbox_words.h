// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "hal/types.h"
#include "infra/seat.h"

namespace mister::proto {

struct MailboxWords {
    TASTY_SEAT_EXEMPT(component);

    static constexpr std::size_t kCount = 3;
    std::array<hal::SpiWord, kCount> w{};

    constexpr MailboxWords() noexcept = default;

    constexpr MailboxWords(hal::SpiWord w0, hal::SpiWord w1, hal::SpiWord w2) noexcept
        : w{w0, w1, w2} {}

    [[nodiscard]] constexpr std::span<const hal::SpiWord, kCount> data() const noexcept {
        return w;
    }

    friend constexpr bool operator==(const MailboxWords&, const MailboxWords&) = default;
};

static_assert(sizeof(MailboxWords) == MailboxWords::kCount * sizeof(hal::SpiWord));

}
