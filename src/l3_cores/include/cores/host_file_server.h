// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cores/host_file_sandbox.h"
#include "cores/mailbox_window.h"
#include "infra/error.h"
#include "svc/vfs.h"

namespace mister::cores {

[[nodiscard]] constexpr bool mailbox_span_fits(
    std::uint32_t off,
    std::uint32_t n,
    std::uint32_t len
) noexcept {
    return n <= len && off <= len - n;
}

static_assert(mailbox_span_fits(0x0FF0u, 0x0010u, 0x1000u));
static_assert(!mailbox_span_fits(0xFFFF'FFF0u, 0x0000'0020u, 0x0000'1000u));

template <class Protocol>
class HostFileServer {
public:
    HostFileServer(const svc::Vfs& vfs, std::string_view share_root) : sandbox_(vfs, share_root) {}

    [[nodiscard]] Ex<void> bind_page(std::span<std::byte> page) {
        window_ = MailboxWindow{};
        if constexpr (Protocol::kDataLength == 0u) {
            return {};
        } else {
            if (!mailbox_span_fits(
                    static_cast<std::uint32_t>(Protocol::kDataOffset),
                    static_cast<std::uint32_t>(Protocol::kDataLength),
                    page.size() > 0xFFFF'FFFFu ? 0xFFFF'FFFFu
                                               : static_cast<std::uint32_t>(page.size())
                )) {
                return std::unexpected(
                    Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(page.size())}
                );
            }
            window_ = MailboxWindow{page.subspan(Protocol::kDataOffset, Protocol::kDataLength)};
            return {};
        }
    }

    void unbind() noexcept { window_ = MailboxWindow{}; }

    MailboxWindow& data_window() noexcept { return window_; }

    struct MailboxOutcome {
        bool served = false;
        std::size_t reply_len = 0;
    };

    [[nodiscard]] Ex<MailboxOutcome> on_mailbox_edge(
        std::uint16_t req_id,
        std::span<const std::byte> request,
        std::span<std::byte> reply_out
    ) {
        if (req_id == last_req_id_) {
            return MailboxOutcome{};
        }
        auto req = Protocol::decode(request);
        if (!req)
            return std::unexpected(req.error());
        auto rep = Protocol::serve(*req, sandbox_, window_);
        if (!rep)
            return std::unexpected(rep.error());
        const std::size_t len = Protocol::encode(*rep, reply_out);
        last_req_id_ = req_id;
        return MailboxOutcome{true, len};
    }

    HostFileSandbox& sandbox() noexcept { return sandbox_; }

private:
    HostFileSandbox sandbox_;
    MailboxWindow window_;
    std::uint16_t last_req_id_ = 0xFFFF;
};

}
