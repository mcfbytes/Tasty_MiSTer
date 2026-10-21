// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/stack_touch.h"

#include "infra/error.h"

namespace mister::os {

[[gnu::noinline]] void touch_stack_pages(std::size_t span, std::size_t page_bytes) noexcept {
    if (page_bytes == 0) {
        fatal(Error{Errc::negotiation, ERR_SITE(), 0}, "touch_stack_pages: zero page size");
    }
    if (span == 0)
        return;
    volatile unsigned char* const p = static_cast<volatile unsigned char*>(__builtin_alloca(span));
    for (std::size_t off = span - 1;; off -= page_bytes) {
        p[off] = 0;
        if (off < page_bytes)
            break;
    }
    p[0] = 0;
}

}
