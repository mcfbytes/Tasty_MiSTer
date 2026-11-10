// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/error.h"

namespace mister::infra {

template <class End>
[[nodiscard]] End wired_end(End end, const char* what) noexcept {
    if (!end)
        fatal(Error{Errc::busy, ERR_SITE(), 0}, what);
    return end;
}

}
