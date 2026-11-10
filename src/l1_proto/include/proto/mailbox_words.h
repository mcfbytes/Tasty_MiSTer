// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/seat.h"

namespace mister::proto {

struct MailboxWords {
    TASTY_SEAT_EXEMPT(component);

    static constexpr std::size_t kCount = 3;
    std::array<std::uint16_t, kCount> w{};

    constexpr MailboxWords() noexcept = default;

    constexpr MailboxWords(std::uint16_t w0, std::uint16_t w1, std::uint16_t w2) noexcept
        : w{w0, w1, w2} {}

    [[nodiscard]] constexpr std::span<const std::uint16_t, kCount> data() const noexcept {
        return w;
    }

    friend constexpr bool operator==(const MailboxWords&, const MailboxWords&) = default;
};

static_assert(sizeof(MailboxWords) == MailboxWords::kCount * sizeof(std::uint16_t));

}
