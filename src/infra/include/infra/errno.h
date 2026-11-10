// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/decimal_text.h"
#include "infra/error.h"
#include "infra/json_out.h"

namespace mister::infra {

struct Errno {
    int v = 0;

    [[nodiscard]] constexpr bool is_set() const noexcept { return v != 0; }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    friend constexpr void json_field(infra::JsonOut& o, infra::JsonName key, Errno e) noexcept {
        o.field(key, e.v);
    }

    [[nodiscard]] constexpr Error refused(Errc errc, ErrorSite at) const noexcept {
        return Error{errc, at, static_cast<std::uint32_t>(v)};
    }

    friend constexpr bool operator==(Errno, Errno) = default;
};

static_assert(sizeof(Errno) == sizeof(int));

}

namespace mister {

constexpr infra::Errno Error::os_errno() const noexcept {
    return infra::Errno{static_cast<int>(detail)};
}

}
