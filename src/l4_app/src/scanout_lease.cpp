// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/scanout_lease.h"

#include <sys/socket.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <span>
#include <string_view>
#include <utility>

#include "app/scanout_packet.h"
#include "infra/byte_view.h"

namespace mister::app {
namespace {

constexpr std::size_t kRecvBytes = 64;

bool send_text(const UniqueFd& socket, std::string_view s) noexcept {
    return ::send(socket.get(), s.data(), s.size(), MSG_DONTWAIT | MSG_NOSIGNAL)
        == static_cast<ssize_t>(s.size());
}

}

void ScanoutLease::offer(UniqueFd socket) noexcept {
    TASTY_SEAT_BODY(ScanoutLease);
    socket_ = std::move(socket);
}

void ScanoutLease::serve(std::chrono::nanoseconds now) noexcept {
    TASTY_SEAT_BODY(ScanoutLease);
    deliver_answers_();
    if (held_ask_ && !burst_in_flight_ && w_.scanout.post(*held_ask_)) {
        held_ask_.reset();
        burst_in_flight_ = true;
    }
    while (socket_.valid()) {
        std::byte buf[kRecvBytes];
        const ssize_t n = ::recv(socket_.get(), buf, sizeof buf, MSG_DONTWAIT);
        if (n == 0) {
            close();
            break;
        }
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                close();
            break;
        }
        on_packet_(std::span(buf).first(static_cast<std::size_t>(n)), now);
    }
    finish_grant_(now);
}

void ScanoutLease::settled(std::chrono::nanoseconds now) noexcept {
    TASTY_SEAT_BODY(ScanoutLease);
    if (request_waiting_)
        grant_(now);
}

void ScanoutLease::grant_(std::chrono::nanoseconds now) noexcept {
    request_waiting_ = false;
    if (!socket_.valid())
        return;
    grant_by_ = now + std::chrono::nanoseconds{w_.hold_limit};
}

void ScanoutLease::finish_grant_(std::chrono::nanoseconds now) noexcept {
    if (!grant_by_ || !socket_.valid())
        return;
    if (!w_.fb_ack.sample().value.held) {
        if (now < *grant_by_)
            return;
        n_.refusals.add();
        (void)send_text(socket_, scanout::kRefuse);
        close();
        return;
    }
    grant_by_.reset();
    if (!send_text(socket_, scanout::kGrant)) {
        close();
        return;
    }
    granted_ = true;
    n_.leases.add();
}

void ScanoutLease::on_packet_(
    std::span<const std::byte> packet,
    std::chrono::nanoseconds now
) noexcept {
    if (!granted_) {
        const std::string_view text = infra::as_text(packet);
        if (text != scanout::kRequest || request_waiting_) {
            n_.refusals.add();
            (void)send_text(socket_, scanout::kRefuse);
            close();
            return;
        }
        request_waiting_ = true;
        if (w_.state.sample().value.settled)
            grant_(now);
        return;
    }

    if (burst_in_flight_ || held_ask_) {
        n_.invalid_packets.add();
        return;
    }
    const auto b = scanout::decode(packet);
    if (!b) {
        n_.invalid_packets.add();
        return;
    }
    if (w_.scanout.post(*b))
        burst_in_flight_ = true;
    else
        held_ask_ = *b;
}

void ScanoutLease::deliver_answers_() noexcept {
    while (const auto a = w_.scanout.take_answer()) {
        burst_in_flight_ = false;
        n_.bursts.add();
        if (!socket_.valid() || !granted_)
            continue;

        if (!a->ok) {
            close();
            continue;
        }
        std::array<std::byte, scanout::kMaxPacketBytes> out{};
        const std::span<const std::byte> reply = scanout::encode_reply(*a, out);
        if (reply.empty()
            || ::send(socket_.get(), reply.data(), reply.size(), MSG_DONTWAIT | MSG_NOSIGNAL)
                != static_cast<ssize_t>(reply.size()))
            close();
    }
}

void ScanoutLease::close() noexcept {
    TASTY_SEAT_BODY(ScanoutLease);
    socket_.reset();
    granted_ = false;
    request_waiting_ = false;
    grant_by_.reset();
    held_ask_.reset();
}

}
