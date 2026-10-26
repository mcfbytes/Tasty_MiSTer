// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/lw_sys_endpoint.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/epoll.h>

#include <algorithm>
#include <chrono>
#include <utility>

#include "infra/lease_order.h"
#include "infra/unique_fd.h"
#include "reactor/notifier.h"

namespace mister::app {

LwSysEndpoint::LwSysEndpoint(const Wiring& w) noexcept : w_(w) {}

LwSysEndpoint::~LwSysEndpoint() {
    if (line_.fd() >= 0 && w_.doorbells >= 0) {
        (void)::epoll_ctl(w_.doorbells, EPOLL_CTL_DEL, line_.fd(), nullptr);
    }
}

void LwSysEndpoint::serve() noexcept {
    TASTY_SEAT_BODY(LwSysEndpoint);
    take_grant_();
    if (bound_gen_ == 0)
        return;
    const bool rang = line_rang_();
    if (!rang && !recheck_)
        return;
    recheck_ = false;
    serve_status_(rang);
}

bool LwSysEndpoint::idle() const noexcept {
    TASTY_SEAT_BODY(LwSysEndpoint);

    if (const auto g = w_.grant.generation(); g != 0 && g != grant_rd_.seen())
        return false;
    return bound_gen_ == 0 || !recheck_;
}

void LwSysEndpoint::on_pause() noexcept {
    TASTY_SEAT_BODY(LwSysEndpoint);
    unbind_();
}

void LwSysEndpoint::on_stop() noexcept {
    TASTY_SEAT_BODY(LwSysEndpoint);
    unbind_();
}

void LwSysEndpoint::on_resume() noexcept {
    TASTY_SEAT_BODY(LwSysEndpoint);
    grant_rd_.forget();
}

void LwSysEndpoint::take_grant_() noexcept {
    const auto g = grant_rd_.take_if_changed(w_.grant);
    if (!g)
        return;

    unbind_();
    if (g->live())
        bind_(*g, grant_rd_.seen());
}

void LwSysEndpoint::bind_(const SysGrant& g, std::uint32_t gen) noexcept {
    grant_ = g;
    bound_gen_ = gen;
    if (!w_.port)
        block_.emplace(hal::reg::Block::redeem(g.block));
    if (!lease_()) {
        answer_bind_(g.gen);
        return;
    }
    if (!arm_line_(g)) {
        release_();
        unbind_();
        ++tally_.bind_refusals;
        answer_bind_(g.gen);
        return;
    }
    release_();
    recheck_ = true;
    sent_stflg_.reset();
    tally_.on_lw = 1;
    answer_bind_(g.gen);
}

void LwSysEndpoint::answer_bind_(std::uint16_t gen) noexcept {
    tally_.bind_gen = gen;
    publish_();
}

bool LwSysEndpoint::arm_line_(const SysGrant& g) noexcept {
    if (w_.doorbells < 0)
        return false;
    const int fd = ::fcntl(g.line.fd(), F_DUPFD_CLOEXEC, 0);
    auto line = os::UioHandle::adopt(g.line.line(), UniqueFd{fd});
    if (!line)
        return false;
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = line->fd();
    if (::epoll_ctl(w_.doorbells, EPOLL_CTL_ADD, line->fd(), &ev) != 0)
        return false;
    line_ = std::move(*line);
    (void)line_.borrow().consume();
    (void)line_.borrow().rearm();
    return true;
}

void LwSysEndpoint::unbind_() noexcept {
    if (bound_gen_ == 0)
        return;
    if (line_.fd() >= 0 && w_.doorbells >= 0) {
        (void)::epoll_ctl(w_.doorbells, EPOLL_CTL_DEL, line_.fd(), nullptr);
    }
    line_ = os::UioHandle{};
    block_.reset();
    grant_ = SysGrant{};
    bound_gen_ = 0;
    recheck_ = false;
    sent_stflg_.reset();
    tally_.on_lw = 0;
    publish_();
}

bool LwSysEndpoint::lease_() noexcept {
    tally_.leased = 1;
    publish_();
    if (xthread::LeaseOrder::grant_unchanged(w_.grant, bound_gen_))
        return true;
    tally_.leased = 0;
    ++tally_.revoked;
    unbind_();
    return false;
}

void LwSysEndpoint::release_() noexcept {
    tally_.leased = 0;
    publish_();
}

void LwSysEndpoint::publish_() noexcept {
    w_.route.publish(tally_);
}

LwSysEndpoint::Port& LwSysEndpoint::port_() noexcept {
    if (w_.port)
        return *w_.port;
    return *block_;
}

bool LwSysEndpoint::line_rang_() const noexcept {
    if (line_.fd() < 0)
        return false;
    pollfd p{line_.fd(), POLLIN, 0};
    return ::poll(&p, 1, 0) > 0 && (p.revents & POLLIN) != 0;
}

std::uint32_t LwSysEndpoint::drain_cause_(bool rang) noexcept {
    if (rang)
        (void)line_.borrow().consume();
    const std::uint32_t w = port_().read(hal::SysBlock::Reg::Cause);
    if (rang)
        (void)line_.borrow().rearm();
    return w;
}

bool LwSysEndpoint::read_snapshot_(proto::StatusSnapshot& into) noexcept {
    using hal::SysBlock;
    const std::uint32_t f0 = port_().read(SysBlock::Reg::Stflg);
    for (std::uint32_t i = 0; i < SysBlock::kSnapshotWords; ++i) {
        const std::uint32_t w = port_().read(SysBlock::status(i));
        into.words[2 * i] = static_cast<std::uint16_t>(w & 0xFFFFu);
        into.words[2 * i + 1] = static_cast<std::uint16_t>(w >> 16);
    }
    const std::uint32_t f1 = port_().read(SysBlock::Reg::Stflg);
    into.stflg = f0;
    return f0 == f1 && SysBlock::stflg_valid(f0);
}

void LwSysEndpoint::serve_status_(bool rang) noexcept {
    const auto t0 = clock_.now();
    if (!lease_())
        return;
    const std::uint32_t cause = drain_cause_(rang);
    const reactor::CauseDecode d = reactor::decode_cause_checked(cause);

    if (cause == 0u && !rang) {
        release_();
        return;
    }
    if (d.refused
        || d.cause != reactor::Cause::Sys
        || (cause & hal::SysBlock::kCauseStatus) == 0u) {
        release_();
        ++tally_.spurious;
        publish_();
        return;
    }
    ++tally_.drains;
    bool stable = false;
    for (unsigned i = 0; i < kMaxReads && !stable; ++i) {
        stable = read_snapshot_(snap_);
        if (!stable)
            ++tally_.retries;
    }
    release_();
    if (!stable) {
        ++tally_.torn;
        recheck_ = true;
        publish_();
        return;
    }

    if (sent_stflg_ != snap_.stflg) {
        snap_.gen = grant_.gen;
        w_.snapshots.publish(snap_);
        sent_stflg_ = snap_.stflg;
        ++tally_.snapshots;
    }
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(clock_.now() - t0);
    tally_.lat_max_us = std::max(tally_.lat_max_us, static_cast<std::uint32_t>(us.count()));
    publish_();
}

}
