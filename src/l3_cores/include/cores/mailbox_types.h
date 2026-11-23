// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "infra/byte_count.h"
#include "infra/error.h"
#include "infra/prelude.h"

namespace mister::cores {

struct MailboxLen;

struct MailboxOffset {
    std::uint32_t v = 0;
    using affine_count = MailboxLen;
};

struct MailboxLen {
    std::uint32_t v = 0;
    using affine_count = MailboxLen;

    [[nodiscard]] static constexpr bool fits(std::size_t n) noexcept {
        return n <= std::numeric_limits<std::uint32_t>::max();
    }
};

[[nodiscard]] constexpr MailboxLen mailbox_len(std::size_t n) noexcept {
    return MailboxLen{
        MailboxLen::fits(n) ? static_cast<std::uint32_t>(n)
                            : std::numeric_limits<std::uint32_t>::max()
    };
}

[[nodiscard]] constexpr MailboxLen mailbox_len(infra::ByteCount n) noexcept {
    return mailbox_len(n.v);
}

[[nodiscard]] inline std::unexpected<Error> span_refused(MailboxOffset off) noexcept {
    return std::unexpected(Error{Errc::slot_range, ERR_SITE(), off.v});
}

[[nodiscard]] inline std::unexpected<Error> page_refused(MailboxLen page) noexcept {
    return std::unexpected(Error{Errc::slot_range, ERR_SITE(), page.v});
}

}
