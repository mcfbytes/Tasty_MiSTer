// SPDX-License-Identifier: GPL-3.0-or-later
#include "open_verdict.h"

#include <utility>

#include "report.h"

namespace mister::fw {

void OpenVerdict::refuse(const char* what, const Error& why) noexcept {
    report(what, why);
    if (first_)
        first_ = std::unexpected(why);
}

}
