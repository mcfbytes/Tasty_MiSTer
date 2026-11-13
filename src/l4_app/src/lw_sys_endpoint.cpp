// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/lw_sys_endpoint.h"

#include <algorithm>
#include <chrono>

#include "infra/seat_owner_io.h"
#include "reactor/cause_decode.h"
#include "reactor/notifier.h"

namespace mister::app {

LwSysEndpoint::LwSysEndpoint(const Wiring& w) noexcept : Base(w.grant, w.lease), w_(w) {}

void LwSysEndpoint::serve() noexcept {
    TASTY_SEAT_BODY(LwSysEndpoint);
    Base::serve();
}

bool LwSysEndpoint::idle() const noexcept {
    TASTY_SEAT_BODY(LwSysEndpoint);
    return Base::idle();
}

void LwSysEndpoint::on_pause() noexcept {
    TASTY_SEAT_BODY(LwSysEndpoint);
    Base::on_pause();
}

void LwSysEndpoint::on_stop() noexcept {
    TASTY_SEAT_BODY(LwSysEndpoint);
    Base::on_stop();
}

void LwSysEndpoint::on_resume() noexcept {
    TASTY_SEAT_BODY(LwSysEndpoint);
    Base::on_resume();
}

void LwSysEndpoint::publish_hook_(const SysRoute& r) noexcept {
    w_.route.publish(r);
}

void LwSysEndpoint::prepare_hook_(const SysGrant& g) noexcept {
    if (!w_.port)
        block_.emplace(hal::reg::Block::redeem(g.block));
}

bool LwSysEndpoint::arm_hook_(const SysGrant& g) noexcept {
    return line_.open(g.line, w_.doorbells);
}

void LwSysEndpoint::bind_hook_(const SysGrant&) noexcept {}

void LwSysEndpoint::bound_hook_() noexcept {
    recheck_ = true;
    sent_stflg_.reset();
}

void LwSysEndpoint::answer_hook_(proto::BuildGeneration gen) noexcept {
    tally_.bind_gen = gen;
    publish_();
}

void LwSysEndpoint::unbind_hook_() noexcept {
    line_.close();
    block_.reset();
    recheck_ = false;
    sent_stflg_.reset();
}

bool LwSysEndpoint::idle_bound_hook_() const noexcept {
    return !recheck_;
}

void LwSysEndpoint::serve_bound_() noexcept {
    const bool rang = line_.rang();
    if (!rang && !recheck_)
        return;
    recheck_ = false;
    serve_status_(rang);
}

LwSysEndpoint::Port& LwSysEndpoint::port_() noexcept {
    if (w_.port)
        return *w_.port;
    return *block_;
}

std::optional<proto::StatusSnapshot> LwSysEndpoint::read_snapshot_() noexcept {
    using hal::SysBlock;
    proto::StatusSnapshot snapshot{};
    const std::optional<SysBlock::Stflg>
        first = port_().read(SysBlock::Reg::Stflg).decoded(SysBlock::decode);

    std::array<std::uint16_t, proto::StatusWord::kWords> words{};
    for (std::uint32_t i = 0; i < SysBlock::kSnapshotWords; ++i) {
        const std::array<std::uint16_t, 2> halves = port_().read(SysBlock::status(i)).halves();
        words[2 * i] = halves[0];
        words[2 * i + 1] = halves[1];
    }

    const std::optional<SysBlock::Stflg>
        second = port_().read(SysBlock::Reg::Stflg).decoded(SysBlock::decode);
    if (!first || first != second)
        return std::nullopt;
    snapshot.stflg = *first;
    snapshot.words = proto::StatusWord{words};
    return snapshot;
}

void LwSysEndpoint::serve_status_(bool rang) noexcept {
    const auto t0 = clock_.now();
    if (!lease_())
        return;
    const std::uint32_t cause = line_.drain(rang, port_(), hal::SysBlock::Reg::Cause);
    const reactor::CauseDecode d = reactor::decode_cause_checked(cause);

    if (cause == 0u && !rang) {
        release_();
        return;
    }
    if (d.is_refused()
        || d.cause() != reactor::Cause::Sys
        || (cause & hal::SysBlock::kCauseStatus) == 0u) {
        release_();
        ++tally_.spurious;
        publish_();
        return;
    }
    ++tally_.drains;
    std::optional<proto::StatusSnapshot> capture{};
    for (unsigned i = 0; i < kMaxReads && !capture; ++i) {
        capture = read_snapshot_();
        if (!capture)
            ++tally_.retries;
    }
    release_();
    if (!capture) {
        ++tally_.torn;
        recheck_ = true;
        publish_();
        return;
    }

    if (sent_stflg_ != capture->stflg) {
        capture->gen = grant_.gen;
        w_.snapshots.publish(*capture);
        sent_stflg_ = capture->stflg;
        ++tally_.snapshots;
    }
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(clock_.now() - t0);
    tally_.lat_max_us = std::max(tally_.lat_max_us, static_cast<std::uint32_t>(us.count()));
    publish_();
}

template class LwBlockEndpoint<SysGrant, SysRoute, seat_owner::Io>;

}

namespace mister::hal {
template class ReadWindowPort<SysBlock, seat_owner::Io>;
}

namespace mister::xthread {
template class GrantLease<app::SysGrant, seat_owner::Io>;
}
