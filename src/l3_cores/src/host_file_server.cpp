// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/host_file_server.h"

namespace mister::cores {

Ex<std::span<std::byte>> MailboxWindow::at(std::size_t off, std::size_t n) const {
    constexpr std::size_t kMax = std::numeric_limits<std::uint32_t>::max();
    if (off > kMax || n > kMax) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(off & 0xFFFF'FFFFu)}
        );
    }
    if (window_.data() == nullptr
        || !mailbox_span_fits(
            static_cast<std::uint32_t>(off),
            static_cast<std::uint32_t>(n),
            static_cast<std::uint32_t>(window_.size())
        )) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(off)}
        );
    }
    return std::span<std::byte>{window_.data() + off, n};
}

Ex<std::string> HostFileSandbox::resolve(std::string_view guest_path) const {
    if (guest_path.empty()) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }

    return unimplemented(ERR_SITE());
}

}
