// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/stack_touch.h"

#include "infra/error.h"

namespace mister::os {

[[gnu::noinline]] void touch_stack_pages(StackWalk walk) noexcept {
    if (!walk.page.usable()) {
        fatal(Error{Errc::negotiation, ERR_SITE(), 0}, "touch_stack_pages: zero page size");
    }
    if (walk.span == 0)
        return;
    volatile unsigned char* const p = static_cast<volatile unsigned char*>(
        __builtin_alloca(walk.span)
    );
    for (std::size_t off = walk.span - 1;; off = walk.page.stepped_from(off)) {
        p[off] = 0;
        if (walk.page.is_last(off))
            break;
    }
    p[0] = 0;
}

}
