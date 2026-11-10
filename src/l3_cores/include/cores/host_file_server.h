// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cores/host_file_sandbox.h"
#include "cores/mailbox_req_id.h"
#include "cores/mailbox_window.h"
#include "infra/error.h"
#include "svc/vfs.h"

namespace mister::cores {

template <class Protocol>
class HostFileServer {
public:
    HostFileServer(const svc::Vfs& vfs, std::string_view share_root) : sandbox_(vfs, share_root) {}

    [[nodiscard]] Ex<void> bind_page(std::span<std::byte> page) {
        window_ = MailboxWindow{};
        if constexpr (Protocol::kDataLength == 0u) {
            return {};
        } else {
            constexpr MailboxSpan kDataWindow{
                .off = {Protocol::kDataOffset},
                .len = {Protocol::kDataLength}
            };
            const MailboxLen page_len = mailbox_len(page.size());
            if (!kDataWindow.fits(page_len))
                return page_refused(page_len);
            window_ = MailboxWindow{kDataWindow.of(page)};
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
        MailboxReqId req_id,
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
    MailboxReqId last_req_id_{};
};

}
