// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/mailbox_window.h"

namespace mister::cores {

Ex<std::span<std::byte>> MailboxWindow::at(MailboxSpan span) const {
    if (window_.data() == nullptr || !span.fits(mailbox_len(window_.size()))) {
        return span_refused(span.off);
    }
    return span.of(window_);
}

}
