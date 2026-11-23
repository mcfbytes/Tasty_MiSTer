// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <limits>
#include <span>

#include "hal/types.h"
#include "infra/byte_count.h"
#include "proto/types.h"

namespace mister::proto {

inline constexpr std::size_t kMailboxBulkBytes = 2352;

inline constexpr FioWordCount kMailboxBusMaxWords{kMailboxBulkBytes / 2};

struct MailboxBusLen {

    FioWordCount words{};
    hal::Width framing = hal::Width::Word;
    std::array<std::byte, 3> pad_{};

    [[nodiscard]] static constexpr MailboxBusLen packed(infra::ByteCount bytes) noexcept {
        return MailboxBusLen{
            .words = FioWordCount::whole_in(bytes, hal::Width::Word),
            .framing = hal::Width::Word
        };
    }

    [[nodiscard]] static constexpr MailboxBusLen widened(infra::ByteCount bytes) noexcept {
        return MailboxBusLen{
            .words = FioWordCount::whole_in(bytes, hal::Width::Byte),
            .framing = hal::Width::Byte
        };
    }

    [[nodiscard]] constexpr infra::ByteCount bytes() const noexcept {
        return words.byte_count_at(framing);
    }

    [[nodiscard]] constexpr FioWordCount link_words() const noexcept { return words; }

    [[nodiscard]] constexpr bool fits() const noexcept {
        return words <= kMailboxBusMaxWords && bytes() <= infra::ByteCount{kMailboxBulkBytes};
    }

    template <class Byte>
    [[nodiscard]] constexpr std::span<Byte> of(
        std::span<Byte, kMailboxBulkBytes> bulk
    ) const noexcept {
        return bytes().first_in(std::span<Byte>(bulk));
    }

    friend constexpr bool operator==(MailboxBusLen, MailboxBusLen) = default;
};

static_assert(MailboxBusLen::packed(infra::ByteCount{2352}).link_words() == FioWordCount{1176});
static_assert(
    MailboxBusLen::packed(infra::ByteCount{2352}).fits()
    && !MailboxBusLen::packed(infra::ByteCount{2354}).fits()
);
static_assert(
    MailboxBusLen::widened(infra::ByteCount{1176}).fits()
    && !MailboxBusLen::widened(infra::ByteCount{1177}).fits()
);
static_assert(
    !MailboxBusLen::widened(infra::ByteCount{std::numeric_limits<std::size_t>::max()}).fits(),
    "a length past the word's range is refused, never wrapped into one that fits"
);

}
