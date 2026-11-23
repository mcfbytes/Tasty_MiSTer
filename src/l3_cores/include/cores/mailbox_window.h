// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/mailbox_types.h"
#include "infra/affine.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/span_cut.h"

namespace mister::cores {

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
