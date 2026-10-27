// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/error.h"
#include "infra/seat.h"

namespace mister::fw {

class OpenVerdict {
    TASTY_SEAT_EXEMPT(boot);

public:
    void refuse(const char* what, const Error& why) noexcept;

    [[nodiscard]] bool ok() const noexcept { return first_.has_value(); }

    [[nodiscard]] Ex<void> result() const noexcept { return first_; }

private:
    Ex<void> first_{};
};

template <class Held>
[[nodiscard]] auto& seated(Held& held, const char* what) noexcept {
    if (!held)
        fatal(Error{Errc::dt_missing, ERR_SITE(), 0}, what);
    return *held;
}

}
