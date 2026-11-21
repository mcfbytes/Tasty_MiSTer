// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include "infra/affine.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/span_cut.h"

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

struct MailboxSpan {
    MailboxOffset off{};
    MailboxLen len{};

    [[nodiscard]] constexpr bool fits(MailboxLen page) const noexcept {
        return infra::affine::fits(off, len, page);
    }

    [[nodiscard]] constexpr std::span<std::byte> of(std::span<std::byte> page) const noexcept {
        return fits(mailbox_len(page.size()))
            ? infra::affine::first(infra::affine::from(page, off), len)
            : std::span<std::byte>{};
    }
};

[[nodiscard]] inline std::unexpected<Error> span_refused(MailboxOffset off) noexcept {
    return std::unexpected(Error{Errc::slot_range, ERR_SITE(), off.v});
}

[[nodiscard]] inline std::unexpected<Error> page_refused(MailboxLen page) noexcept {
    return std::unexpected(Error{Errc::slot_range, ERR_SITE(), page.v});
}

static_assert(MailboxSpan{.off = {0x0FF0u}, .len = {0x0010u}}.fits(MailboxLen{0x1000u}));
static_assert(
    !MailboxSpan{.off = {0xFFFF'FFF0u}, .len = {0x0000'0020u}}.fits(MailboxLen{0x0000'1000u})
);

class MailboxWindow {
public:
    MailboxWindow() = default;

    explicit MailboxWindow(std::span<std::byte> window) noexcept : window_(window) {

        if (!MailboxLen::fits(window.size()))
            window_ = {};
    }

    [[nodiscard]] Ex<std::span<std::byte>> at(MailboxSpan span) const;

    [[nodiscard]] std::size_t size() const noexcept { return window_.size(); }

    [[nodiscard]] bool empty() const noexcept { return window_.empty(); }

private:
    std::span<std::byte> window_{};
};

}
