// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/disc_read_service.h"

#include <algorithm>
#include <cstring>

namespace mister::svc {

DiscReadService::DiscReadService(const Wiring& w) noexcept
    : chan_(xthread::Polled{}), mounts_(xthread::Polled{}), geom_cell_(w.geometry),
      count_cell_(w.counters), reader_(w.reader ? &*w.reader : nullptr),
      mounter_(w.mounter ? &*w.mounter : nullptr) {}

DiscReadService::DiscReadService(xthread::WakeFlag& io_wake, const Wiring& w) noexcept
    : chan_(io_wake), mounts_(io_wake), geom_cell_(w.geometry), count_cell_(w.counters),
      reader_(w.reader ? &*w.reader : nullptr), mounter_(w.mounter ? &*w.mounter : nullptr) {}

void DiscReadService::set_generation(std::uint32_t g) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiscReadService, A);
    if (g == gen_) return;
    gen_ = g;

    for (std::size_t i = 0; i < kDiscReadDepth; ++i) {
        resident_[i] = Loan{};
        resident_at_[i] = 0;
    }
    for (auto& f : inflight_)
        f.live = false;
    failed_ = FailedRead{};
}

void DiscReadService::drain_() noexcept {
    for (;;) {
        Loan l = chan_.reap();
        if (!l) return;
        const DiscReadSlot& s = *l;
        for (auto& f : inflight_) {
            if (f.live && f.lba == s.lba && f.form == s.form) {
                f.live = false;
                break;
            }
        }
        if (s.gen != gen_) {
            stale_drops_.add(1);
            continue;
        }
        if (s.ok == 0) {
            failed_ = FailedRead{s.lba, s.form, true};
            continue;
        }

        static_assert(sizeof(resident_) / sizeof(resident_[0]) == kDiscReadDepth);
        for (std::size_t i = 0; i < kDiscReadDepth; ++i) {
            if (!resident_[i]) {
                resident_[i] = std::move(l);
                resident_at_[i] = ++arrivals_;
                break;
            }
        }
    }
}

bool DiscReadService::in_flight_(DiscForm form, std::uint32_t lba) const noexcept {
    for (const auto& f : inflight_) {
        if (f.live && f.lba == lba && f.form == form) return true;
    }
    return false;
}

bool DiscReadService::evict_oldest_resident_() noexcept {
    std::size_t victim = kDiscReadDepth;
    std::uint32_t oldest = 0;
    for (std::size_t i = 0; i < kDiscReadDepth; ++i) {
        if (!resident_[i]) continue;

        const std::uint32_t age = arrivals_ - resident_at_[i];
        if (victim == kDiscReadDepth || age > oldest) {
            victim = i;
            oldest = age;
        }
    }
    if (victim == kDiscReadDepth) return false;
    resident_[victim] = Loan{};
    resident_at_[victim] = 0;
    evictions_.add(1);
    return true;
}

bool DiscReadService::issue_(DiscForm form, std::uint32_t lba) noexcept {
    if (reader_ == nullptr) {
        refusals_.add(1);
        return false;
    }
    if (in_flight_(form, lba)) return true;
    Loan l = chan_.acquire();
    if (!l && evict_oldest_resident_()) l = chan_.acquire();
    if (!l) {
        refusals_.add(1);
        return false;
    }
    DiscReadSlot& s = *l;
    s.gen = gen_;
    s.lba = lba;
    s.form = form;
    s.want = static_cast<std::uint16_t>(disc_form_bytes(form));
    s.got = 0;
    s.ok = 0;
    for (auto& f : inflight_) {
        if (!f.live) {
            f = Inflight{lba, form, true};
            break;
        }
    }
    chan_.send(std::move(l));
    return true;
}

std::size_t DiscReadService::take(DiscForm form, proto::Lba lba,
                                  std::span<std::byte> dst) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiscReadService, A);
    drain_();
    for (std::size_t i = 0; i < kDiscReadDepth; ++i) {
        if (!resident_[i]) continue;
        const DiscReadSlot& s = *resident_[i];
        if (s.lba != lba.v || s.form != form) continue;
        const std::size_t n = std::min<std::size_t>(s.got, dst.size());
        std::memcpy(dst.data(), s.data, n);
        resident_[i] = Loan{};
        resident_at_[i] = 0;
        return n;
    }
    not_resident_.add(1);
    (void)issue_(form, lba.v);
    return 0;
}

bool DiscReadService::take_read_failed(DiscForm form, proto::Lba lba) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiscReadService, A);
    if (!failed_.live || failed_.form != form || failed_.lba != lba.v) return false;
    failed_.live = false;
    return true;
}

void DiscReadService::hint(DiscForm form, proto::Lba lba) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiscReadService, A);
    drain_();
    for (const auto& r : resident_) {
        if (r && (*r).lba == lba.v && (*r).form == form) return;
    }
    (void)issue_(form, lba.v);
}

void DiscReadService::serve_(DiscReadSlot& s) noexcept {
    s.got = 0;
    s.ok = 0;
    if (reader_ == nullptr) return;
    auto r = reader_->read_form(s.form, proto::Lba{s.lba}, std::span<std::byte>(s.data, s.want));
    reads_.add(1);
    if (!r) {
        read_errors_.add(1);
        return;
    }
    s.got = static_cast<std::uint16_t>(*r);
    s.ok = 1;
}

DiscMountState DiscReadService::arm_mount(DiscOp op, const CuePolicy* policy,
                                          std::string_view path) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiscReadService, A);
    poll_mounts_();

    if (mount_live_) return DiscMountState::Pending;
    if (mount_state_ != DiscMountState::Idle && op == armed_op_ && armed_path_.view() == path) {
        return mount_state_;
    }
    if (mounter_ == nullptr) {
        refusals_.add(1);
        return DiscMountState::Failed;
    }
    auto l = mounts_.acquire();
    if (!l) {
        refusals_.add(1);
        return DiscMountState::Pending;
    }

    set_generation(gen_ + 1);
    if (!armed_path_.assign(path)) {
        refusals_.add(1);
        return DiscMountState::Failed;
    }
    armed_op_ = op;
    DiscMountSlot& m = *l;
    (void)m.path.assign(path);
    m.policy = policy;
    m.gen = gen_;
    m.op = op;
    m.mounted = 0;
    m.done = 0;
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
        m.policy = nullptr;
        m.gen = gen_;
        m.op = DiscOp::Release;
        m.mounted = 0;
        m.done = 0;
        mounts_.send(std::move(l));
    } else {
        refusals_.add(1);
    }

    set_generation(gen_ + 1);
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
        if (!l) return;
        const DiscMountSlot& m = *l;
        if (m.gen != gen_) continue;
        mount_live_ = false;
        mount_state_ = m.mounted != 0 ? DiscMountState::Mounted : DiscMountState::Failed;
    }
}

DiscMountState DiscReadService::mount_state() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiscReadService, A);
    poll_mounts_();
    return mount_live_ ? DiscMountState::Pending : mount_state_;
}

const DiscGeometry& DiscReadService::geometry() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiscReadService, A);
    if (auto g = geom_rd_.take_if_changed(geom_cell_); g.has_value()) geom_ = *g;
    return geom_;
}

const DiscCounters& DiscReadService::counters() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiscReadService, A);
    if (auto c = count_rd_.take_if_changed(count_cell_); c.has_value()) count_ = *c;
    return count_;
}

void DiscReadService::serve_mounts_io_() noexcept {
    for (;;) {
        auto j = mounts_.take();
        if (!j) return;
        DiscMountSlot& m = *j;
        m.mounted = (mounter_ != nullptr && mounter_->apply(m.op, m.policy, m.path.view())) ? 1 : 0;
        m.done = 1;
        j.complete();
    }
}

void DiscReadService::serve() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiscReadService, B);
    serve_mounts_io_();
    for (;;) {
        auto j = chan_.take();
        if (!j) return;
        serve_(*j);
        j.complete();
    }
}

bool DiscReadService::idle() const noexcept {
    return chan_.outbound() == 0 && mounts_.outbound() == 0;
}

void DiscReadService::pump_on_caller() noexcept {
    serve_mounts_io_();
    for (;;) {
        auto j = chan_.take();
        if (!j) return;
        serve_(*j);
        j.complete();
    }
}

}  // namespace mister::svc
