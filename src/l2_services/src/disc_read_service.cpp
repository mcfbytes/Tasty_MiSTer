// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/disc_ask.h"
#include "svc/disc_read_service.h"
#include "svc/disc_take.h"

#include <algorithm>
#include <cstring>

#include "infra/polled.h"

namespace mister::svc {

DiscReadService::DiscReadService(const Wiring& w) noexcept
    : chan_(xthread::Polled{}),
      mounts_(xthread::Polled{}),
      geom_cell_(w.geometry),
      count_cell_(w.counters),
      reader_(w.reader),
      mounter_(w.mounter) {}

DiscReadService::DiscReadService(xthread::WakeFlag& io_wake, const Wiring& w) noexcept
    : chan_(io_wake),
      mounts_(io_wake),
      geom_cell_(w.geometry),
      count_cell_(w.counters),
      reader_(w.reader),
      mounter_(w.mounter) {}

void DiscReadService::set_generation(Generation g) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiscReadService, A);
    if (g == gen_)
        return;
    gen_ = g;

    for (Resident& r : resident_)
        r.drop();
    inflight_ = {};
}

void DiscReadService::drain_() noexcept {
    for (;;) {
        Loan l = chan_.reap();
        if (!l)
            return;
        const DiscReadSlot& s = *l;
        const DiscAsk done = s.ask();
        for (std::optional<DiscAsk>& f : inflight_) {
            if (f == done) {
                f.reset();
                break;
            }
        }
        if (s.gen != gen_) {
            stale_drops_.add(1);
            continue;
        }

        for (Resident& r : resident_) {
            if (!r.held()) {
                arrivals_ = arrivals_.next();
                r.place(std::move(l), arrivals_);
                break;
            }
        }
    }
}

bool DiscReadService::in_flight_(DiscAsk ask) const noexcept {
    for (const std::optional<DiscAsk>& f : inflight_) {
        if (f == ask)
            return true;
    }
    return false;
}

bool DiscReadService::evict_oldest_resident_() noexcept {
    Resident* victim = nullptr;
    for (Resident& r : resident_) {
        if (!r.held())
            continue;
        if (!victim || r.older_than(*victim, arrivals_))
            victim = &r;
    }
    if (!victim)
        return false;
    victim->drop();
    evictions_.add(1);
    return true;
}

bool DiscReadService::issue_(DiscAsk ask) noexcept {
    if (!reader_) {
        refusals_.add(1);
        return false;
    }
    if (in_flight_(ask))
        return true;
    Loan l = chan_.acquire();
    if (!l && evict_oldest_resident_())
        l = chan_.acquire();
    if (!l) {
        refusals_.add(1);
        return false;
    }
    DiscReadSlot& s = *l;
    s.gen = gen_;
    s.lba = ask.lba;
    s.form = ask.form;
    s.header = ask.header;
    s.want = static_cast<std::uint16_t>(disc_form_bytes(ask.form));
    s.got = 0;
    s.ok = false;
    for (std::optional<DiscAsk>& f : inflight_) {
        if (!f) {
            f = ask;
            break;
        }
    }
    chan_.send(std::move(l));
    return true;
}

DiscTake DiscReadService::take(DiscAsk ask, std::span<std::byte> dst) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiscReadService, A);
    drain_();
    for (Resident& r : resident_) {
        if (!r.matches(ask))
            continue;
        if (!r.read().ok) {
            r.drop();
            return DiscTake::failed();
        }
        const DiscReadSlot& s = r.read();
        const std::size_t n = std::min<std::size_t>(s.got, dst.size());
        std::memcpy(dst.data(), s.data, n);
        r.drop();
        return DiscTake::bytes(proto::FileSize{n});
    }
    not_resident_.add(1);
    (void)issue_(ask);
    return DiscTake::not_resident();
}

void DiscReadService::hint(DiscAsk ask) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiscReadService, A);
    drain_();
    for (const Resident& r : resident_) {
        if (r.matches(ask))
            return;
    }
    (void)issue_(ask);
}

void DiscReadService::serve_(DiscReadSlot& s) noexcept {
    s.got = 0;
    s.ok = false;
    if (!reader_)
        return;
    auto r = reader_->read_form(
        s.form,
        proto::Lba{s.lba},
        std::span<std::byte>(s.data, s.want),
        s.header
    );
    reads_.add(1);
    if (!r) {
        read_errors_.add(1);
        return;
    }
    s.got = static_cast<std::uint16_t>(r->size());
    s.ok = true;
}

DiscMountState DiscReadService::arm_mount(
    DiscOp op,
    infra::OptRef<const CuePolicy> policy,
    std::string_view path,
    infra::OptRef<const IDiscIdentity> identity
) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiscReadService, A);
    poll_mounts_();

    if (mount_live_)
        return DiscMountState::Pending;
    if (mount_state_ != DiscMountState::Idle && op == armed_op_ && armed_path_.view() == path) {
        return mount_state_;
    }
    if (!mounter_) {
        refusals_.add(1);
        return DiscMountState::Failed;
    }
    auto l = mounts_.acquire();
    if (!l) {
        refusals_.add(1);
        return DiscMountState::Pending;
    }

    set_generation(gen_.next());
    if (!armed_path_.assign(path)) {
        refusals_.add(1);
        return DiscMountState::Failed;
    }
    armed_op_ = op;
    DiscMountSlot& m = *l;
    (void)m.path.assign(path);
    m.policy = policy;
    m.identity = identity;
    m.gen = gen_;
    m.op = op;
    m.mounted = false;
    mount_live_ = true;
    mount_state_ = DiscMountState::Pending;
    mounts_.send(std::move(l));
    return DiscMountState::Pending;
}

void DiscReadService::release() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiscReadService, A);
    poll_mounts_();
    if (auto l = mounts_.acquire()) {
        DiscMountSlot& m = *l;
        m.path.clear();
        m.policy.reset();
        m.identity.reset();
        m.gen = gen_;
        m.op = DiscOp::Release;
        m.mounted = false;
        mounts_.send(std::move(l));
    } else {
        refusals_.add(1);
    }

    set_generation(gen_.next());
    armed_path_.clear();
    armed_op_ = DiscOp::Mount;
    mount_state_ = DiscMountState::Idle;
    mount_live_ = false;
    (void)geom_rd_.take_if_changed(geom_cell_);
    geom_ = DiscGeometry{};
}

void DiscReadService::poll_mounts_() noexcept {
    for (;;) {
        auto l = mounts_.reap();
        if (!l)
            return;
        const DiscMountSlot& m = *l;
        if (m.gen != gen_)
            continue;
        mount_live_ = false;
        mount_state_ = m.mounted ? DiscMountState::Mounted : DiscMountState::Failed;
    }
}

DiscMountState DiscReadService::mount_state() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiscReadService, A);
    poll_mounts_();
    return mount_live_ ? DiscMountState::Pending : mount_state_;
}

const DiscGeometry& DiscReadService::geometry() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiscReadService, A);
    if (auto g = geom_rd_.take_if_changed(geom_cell_); g.has_value())
        geom_ = *g;
    return geom_;
}

const DiscCounters& DiscReadService::counters() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiscReadService, A);
    if (auto c = count_rd_.take_if_changed(count_cell_); c.has_value())
        count_ = *c;
    return count_;
}

void DiscReadService::serve_mounts_io_() noexcept {
    for (;;) {
        auto j = mounts_.take();
        if (!j)
            return;
        DiscMountSlot& m = *j;
        m.mounted = (mounter_ && mounter_->apply(m.op, m.policy, m.path.view(), m.identity));
        j.complete();
    }
}

void DiscReadService::serve() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiscReadService, B);
    serve_mounts_io_();
    for (;;) {
        auto j = chan_.take();
        if (!j)
            return;
        serve_(*j);
        j.complete();
    }
}

void DiscReadService::serve_on_io(DiscReadSlot& s) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiscReadService, B);
    serve_(s);
}

bool DiscReadService::idle() const noexcept {
    return chan_.outbound() == 0 && mounts_.outbound() == 0;
}

void DiscReadService::pump_on_caller() noexcept {
    serve_mounts_io_();
    for (;;) {
        auto j = chan_.take();
        if (!j)
            return;
        serve_(*j);
        j.complete();
    }
}

}
