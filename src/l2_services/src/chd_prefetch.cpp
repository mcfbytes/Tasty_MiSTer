// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/chd_prefetch.h"

#include <ctime>

#include <new>

namespace mister::svc {

namespace {

std::int64_t decode_now_ns() noexcept {
    constexpr std::int64_t kNsPerSecond = 1'000'000'000;
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * kNsPerSecond
        + static_cast<std::int64_t>(ts.tv_nsec);
}
}

namespace {

constexpr infra::ParkTimeout kRunningPollMs{100};
constexpr infra::ParkTimeout kParkedPollMs = infra::kForever;
}

Ex<void> ChdPrefetch::open() {
    if (slab_ == nullptr) {

        slab_.reset(new (std::nothrow) std::byte[kPrefetchBudgetBytes]);
        if (slab_ == nullptr)
            return std::unexpected(Error{Errc::os, ERR_SITE(), 0});
    }
    if (opened())
        return {};
    return wake_.open_fd();
}

void ChdPrefetch::rest_(infra::ParkTimeout ms, HunkIndex want, bool watch_free) noexcept {
    rests_ = true;
    rest_parked_ = false;
    rest_ms_ = ms;
    want_seen_ = want;
    watch_free_ = watch_free;
}

bool ChdPrefetch::idle() const noexcept {
    if (!rests_)
        return false;
    if (rest_parked_)
        return parked() && attaches_.empty();
    return !(
        !attaches_.empty()
        || park_req_.load(std::memory_order_acquire)
        || want_hunk_.load(std::memory_order_acquire) != want_seen_
        || (watch_free_ && chan_.outbound() != 0)
    );
}

void ChdPrefetch::request_park() noexcept {
    park_req_.store(true, std::memory_order_release);
    wake_.kick();
}

bool ChdPrefetch::park_acked() noexcept {
    request_park();
    return parked();
}

void ChdPrefetch::reset_consumer_() noexcept {

    for (std::size_t i = 0; i < resident_count_; ++i)
        resident_[i] = Chan::Loan{};
    resident_count_ = 0;
    (void)chan_.reclaim_all_parked();
    want_ = HunkIndex::none();
}

bool ChdPrefetch::settle_park() noexcept {

    if (!opened())
        return true;
    request_park();
    if (!parked()) {

        poisoned_ = true;
        return false;
    }
    reset_consumer_();
    poisoned_ = false;
    return true;
}

Ex<void> ChdPrefetch::attach(std::unique_ptr<IChdSource> src, ChdGeometry geo) noexcept {

    if (!parked())
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    if (!src || geo.hunk_extent() == 0 || geo.hunk_count == HunkCount{}) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), geo.hunk_detail()});
    }
    if (!opened())
        return std::unexpected(Error{Errc::io, ERR_SITE(), 0});

    depth_ = HunkCount{};
    hunk_extent_ = 0;
    poisoned_ = false;

    const std::optional<HunkCount> depth = derive_depth(kPrefetchBudgetBytes, geo.hunk_bytes);
    if (!depth) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), geo.hunk_detail()});
    }

    if (slab_ == nullptr) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), geo.hunk_detail()});
    }
    if (geo.extent_of(*depth) > kPrefetchBudgetBytes) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), geo.hunk_detail()});
    }

    for (std::size_t i = 0; i < resident_count_; ++i)
        resident_[i] = Chan::Loan{};
    resident_count_ = 0;
    want_ = HunkIndex::none();
    (void)chan_.reclaim_all_parked();
    want_hunk_.store(HunkIndex::none(), std::memory_order_release);

    hunk_extent_ = geo.hunk_extent();
    depth_ = *depth;

    const HunkCount avail = depth_ - kPrefetchLookBehind;
    ChdMedia media;
    media.src = std::move(src);
    media.geo = geo;
    media.forward_span = avail < kPrefetchForwardSpan ? avail : kPrefetchForwardSpan;

    if (!chan_.set_live(static_cast<std::uint8_t>(depth_.v))) {
        depth_ = HunkCount{};
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    }
    for (HunkCount seeded{}; seeded < depth_; seeded += kOneHunk)
        chan_.send(chan_.acquire());

    if (auto r = hand_over_(std::move(media)); !r) {
        (void)chan_.reclaim_all_parked();
        depth_ = HunkCount{};
        return r;
    }

    park_req_.store(false, std::memory_order_release);
    parked_.store(false, std::memory_order_release);
    wake_.kick();
    return {};
}

bool ChdPrefetch::reap_leases_() noexcept {
    const HandoverSeq taken = attaches_taken_.load(std::memory_order_acquire);
    for (HeldLease& held : cmd_.leases) {
        if (held.lease && taken.reached(held.seq))
            held.lease.reset();
    }
    return media_.available() != 0;
}

Ex<void> ChdPrefetch::hand_over_(ChdMedia&& media) noexcept {
    (void)reap_leases_();
    auto lease = media_.acquire();
    if (!lease)
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    *lease = std::move(media);
    const xthread::PoolSlot idx = *media_.index_of(lease.get());
    if (!attaches_.push(ChdAttach{idx})) {
        *lease = ChdMedia{};
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    }
    cmd_.sent = cmd_.sent.next();
    cmd_.leases[idx].seq = cmd_.sent;
    cmd_.leases[idx].lease = std::move(lease);
    return {};
}

void ChdPrefetch::take_attaches_() noexcept {
    while (const auto a = attaches_.pop()) {
        held_ = Chan::Job{};
        ChdMedia& m = media_.at(a->lease);
        src_ = std::move(m.src);
        geo_ = m.geo;
        forward_span_ = m.forward_span;
        next_ = HunkIndex{};
        tracking_ = HunkIndex::none();
        disabled_ = false;
        taken_ = taken_.next();
        attaches_taken_.store(taken_, std::memory_order_release);
    }
}

void ChdPrefetch::release_media_() noexcept {

    take_attaches_();
    held_ = Chan::Job{};
    src_.reset();
    tracking_ = HunkIndex::none();
    next_ = HunkIndex{};
    disabled_ = false;
}

void ChdPrefetch::serve() noexcept {

    TASTY_SEAT_BODY_MEDIATOR(ChdPrefetch, A);
    rests_ = false;
    take_attaches_();
    if (park_due(parked_, park_req_)) {
        release_media_();
        parked_.store(true, std::memory_order_release);
    }
    if (parked()) {
        rests_ = true;
        rest_parked_ = true;
        rest_ms_ = kParkedPollMs;
        return;
    }

    const HunkIndex want = want_hunk_.load(std::memory_order_acquire);
    if (want != HunkIndex::none() && want != tracking_) {
        if (tracking_ == HunkIndex::none()) {

            tracking_ = want;
            next_ = want;
        } else if (want > next_ || (want < tracking_ && (tracking_ - want) > kPrefetchLookBehind)) {

            tracking_ = want;
            next_ = want;
            seeks_.add();
        } else {

            tracking_ = want;
        }
    }

    const HunkCount lead = next_ - tracking_;
    const bool no_work = disabled_
        || (src_ == nullptr)
        || (want == HunkIndex::none())
        || !geo_.hunk_count.contains(next_)
        || (lead >= forward_span_);

    Chan::Job job;
    if (!no_work) {
        if (held_) {
            job = std::move(held_);
        } else {
            job = chan_.take();
        }
    }
    if (no_work || !job) {
        rest_(kRunningPollMs, want, !no_work);
        return;
    }

    const HunkIndex h = next_;
    const std::span<std::byte> dst = slot_bytes_(job.slot());
    constexpr std::int64_t kNsPerMicrosecond = 1000;
    const std::int64_t t0 = decode_now_ns();
    auto r = src_->read_hunk(h, dst);
    decode_us_.add(static_cast<std::uint32_t>((decode_now_ns() - t0) / kNsPerMicrosecond));
    if (!r) {
        errors_.add();

        held_ = std::move(job);
        disabled_ = true;
        return;
    }
    decodes_.add();
    job->hunk = h;
    job.complete();
    job = Chan::Job{};
    next_ = next_ + kOneHunk;

    if (!sprinting_())
        rest_(kPrefetchTopUpMs, want, false);
}

void ChdPrefetch::release() noexcept {
    release_media_();
    parked_.store(true, std::memory_order_release);
}

void ChdPrefetch::park_now() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(ChdPrefetch, A);
    if (parked())
        return;
    release_media_();
    parked_.store(true, std::memory_order_release);
}

void ChdPrefetch::kick_worker_() noexcept {
    wake_.kick_if_armed();
}

std::size_t ChdPrefetch::evict_(HunkIndex current) noexcept {

    const HunkCount behind = current - HunkIndex{};
    const HunkCount look = behind < kPrefetchLookBehind ? behind : kPrefetchLookBehind;
    const HunkIndex lo = current - look;
    std::size_t w = 0;
    std::size_t released = 0;
    for (std::size_t i = 0; i < resident_count_; ++i) {
        const HunkIndex h = resident_hunk_[i];
        const bool in_window = h >= lo && (h - lo) < depth_;
        if (!in_window) {
            chan_.send(std::move(resident_[i]));
            ++released;
            continue;
        }
        resident_hunk_[w] = resident_hunk_[i];
        resident_[w++] = std::move(resident_[i]);
    }
    resident_count_ = w;
    if (released != 0)
        evictions_.add(static_cast<std::uint32_t>(released));
    return released;
}

std::span<std::byte> ChdPrefetch::slot_bytes_(xthread::PoolSlot slot) const noexcept {
    return slot.subspan_of(
        std::span<std::byte>(slab_.get(), kPrefetchBudgetBytes),
        xthread::SlotStride{hunk_extent_}
    );
}

std::span<const std::byte> ChdPrefetch::take(HunkIndex hunk) noexcept {
    if (!attached() || poisoned_)
        return {};

    bool kicked = false;
    if (hunk != want_) {
        want_ = hunk;
        want_hunk_.store(hunk, std::memory_order_release);
        kick_worker_();
        kicked = true;
    }

    for (;;) {
        auto l = chan_.reap();
        if (!l)
            break;

        if (!l.completed() || HunkCount::of_slots(resident_count_) >= depth_) {
            chan_.send(std::move(l));
            continue;
        }
        resident_hunk_[resident_count_] = l->hunk;
        resident_[resident_count_++] = std::move(l);
    }

    if (evict_(hunk) != 0 && !kicked)
        kick_worker_();

    for (std::size_t i = 0; i < resident_count_; ++i) {
        if (resident_hunk_[i] == hunk) {
            hits_.add();
            return slot_bytes_(*chan_.index_of(resident_[i]));
        }
    }
    return {};
}

PrefetchState ChdPrefetch::state() const noexcept {
    if (!opened())
        return PrefetchState::Closed;
    return parked() ? PrefetchState::Parked : PrefetchState::Running;
}

PrefetchCounters ChdPrefetch::counters() const noexcept {
    PrefetchCounters c;
    c.hits = hits_.get();
    c.misses = misses_.get();
    c.decodes = decodes_.get();
    c.seeks = seeks_.get();

    c.drops = chan_.breaches();
    c.errors = errors_.get();
    c.state = state();
    c.decode_us = decode_us_.get();
    c.evictions = evictions_.get();
    return c;
}

}
