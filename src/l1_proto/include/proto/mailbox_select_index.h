// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "infra/affine.h"
#include "proto/mailbox_select_count.h"

namespace mister::proto {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator*;
using ::mister::infra::affine::operator/;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

struct MailboxSelectIndex {
    std::uint8_t v = 0;
    using affine_count = MailboxSelectCount;
    using affine_index = MailboxSelectIndex;

    [[nodiscard]] constexpr MailboxSelectIndex next() const noexcept {
        return *this + MailboxSelectCount{1};
    }

    [[nodiscard]] constexpr bool within(MailboxSelectCount selects) const noexcept {
        return infra::affine::in_bounds(selects, *this);
    }

    template <class Row>
    [[nodiscard]] constexpr const Row& in(std::span<const Row> rows) const noexcept {
        return rows[v];
    }

    friend constexpr bool operator==(MailboxSelectIndex, MailboxSelectIndex) = default;
    friend constexpr auto operator<=>(MailboxSelectIndex, MailboxSelectIndex) = default;
};

}
