// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include "cores/host_file_sandbox.h"
#include "cores/mailbox_req_id.h"
#include "cores/mailbox_window.h"
#include "infra/byte_count.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "svc/vfs.h"

namespace mister::cores {

template <class P>
concept HostFileProtocol = std::move_constructible<P>
    && requires(P& protocol,
                std::span<const std::byte> request,
                std::span<std::byte> reply_out,
                typename P::Request& asked,
                const typename P::Reply& answered,
                HostFileSandbox& sandbox,
                MailboxWindow& window) {
           { P::kDataOffset } -> std::convertible_to<std::size_t>;
           { P::kDataLength } -> std::convertible_to<std::size_t>;
           { protocol.decode(request) } -> std::same_as<Ex<typename P::Request>>;
           { protocol.serve(asked, sandbox, window) } -> std::same_as<Ex<typename P::Reply>>;
           { protocol.encode(answered, reply_out) } -> std::same_as<infra::ByteCount>;
       };

template <HostFileProtocol Protocol>
class HostFileServer {
    TASTY_SEAT_RESIDENT(Io);

public:
    HostFileServer(const svc::Vfs& vfs, std::string_view share_root, Protocol protocol = {})
        : sandbox_(vfs, share_root),
          protocol_(std::move(protocol)) {}

    [[nodiscard]] Ex<void> bind_page(std::span<std::byte> page) {
        window_ = MailboxWindow{};
        last_req_id_ = MailboxReqId{};
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
        infra::ByteCount reply_len{};
    };

    [[nodiscard]] Ex<MailboxOutcome> on_mailbox_edge(
        MailboxReqId req_id,
        std::span<const std::byte> request,
        std::span<std::byte> reply_out
    ) {
        if (req_id == last_req_id_) {
            return MailboxOutcome{};
        }
        auto len = serve_request(request, reply_out);
        if (!len)
            return std::unexpected(len.error());
        last_req_id_ = req_id;
        return MailboxOutcome{true, *len};
    }

    [[nodiscard]] Ex<infra::ByteCount> serve_request(
        std::span<const std::byte> request,
        std::span<std::byte> reply_out
    ) {
        auto req = protocol_.decode(request);
        if (!req)
            return std::unexpected(req.error());
        auto rep = protocol_.serve(*req, sandbox_, window_);
        if (!rep)
            return std::unexpected(rep.error());
        return protocol_.encode(*rep, reply_out);
    }

    HostFileSandbox& sandbox() noexcept { return sandbox_; }

    Protocol& protocol() noexcept { return protocol_; }

private:
    HostFileSandbox sandbox_;
    Protocol protocol_;
    MailboxWindow window_;
    MailboxReqId last_req_id_{};
};

}
