// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/chd_prefetch.h"

#include <ctime>

#include <new>

namespace mister::svc {

namespace {

std::int64_t decode_now_ns() noexcept {
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000 +
           static_cast<std::int64_t>(ts.tv_nsec);
}
}  // namespace

namespace {

constexpr int kRunningPollMs = 100;
constexpr int kParkedPollMs = -1;
}  // namespace

Ex<void> ChdPrefetch::open() {
    if (slab_ == nullptr) {

        slab_.reset(new (std::nothrow) std::byte[kPrefetchBudgetBytes]);
        if (slab_ == nullptr) return std::unexpected(Error{Errc::os, ERR_SITE(), 0});
    }
    if (opened()) return {};
    return wake_.open_fd();
}

void ChdPrefetch::rest_(int ms, std::uint32_t want, bool watch_free) noexcept {
    rests_ = true;
    rest_parked_ = false;
    rest_ms_ = ms;
    want_seen_ = want;
    watch_free_ = watch_free;
}

bool ChdPrefetch::idle() const noexcept {
    if (!rests_) return false;
    if (rest_parked_) return parked();
    return !(park_req_.load(std::memory_order_acquire) ||
             want_hunk_.load(std::memory_order_acquire) != want_seen_ ||
             (watch_free_ && chan_.outbound() != 0));
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
    want_ = kNoPrefetchHunk;
}

bool ChdPrefetch::settle_park() noexcept {

    if (!opened()) return true;
    request_park();
    if (!parked()) {

        poisoned_ = true;
        return false;
    }
    reset_consumer_();
    poisoned_ = false;
    return true;
}

Ex<void> ChdPrefetch::attach(std::unique_ptr<IChdSource> src, std::uint32_t hunk_bytes,
                             std::uint32_t hunk_count) noexcept {

    if (!parked()) return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    if (!src || hunk_bytes == 0 || hunk_count == 0) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), hunk_bytes});
    }
    if (!opened()) return std::unexpected(Error{Errc::io, ERR_SITE(), 0});

    depth_ = 0;
    forward_span_ = 0;
    hunk_bytes_ = 0;
    hunk_count_ = 0;
    poisoned_ = false;

    const std::size_t depth = derive_depth(kPrefetchBudgetBytes, hunk_bytes);
    if (depth == 0) return std::unexpected(Error{Errc::bad_format, ERR_SITE(), hunk_bytes});

    if (slab_ == nullptr) return std::unexpected(Error{Errc::os, ERR_SITE(), hunk_bytes});
    if (static_cast<std::size_t>(hunk_bytes) * depth > kPrefetchBudgetBytes) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), hunk_bytes});
    }

    for (std::size_t i = 0; i < resident_count_; ++i)
        resident_[i] = Chan::Loan{};
    resident_count_ = 0;
    want_ = kNoPrefetchHunk;
    held_ = Chan::Job{};
    (void)chan_.reclaim_all_parked();
    want_hunk_.store(kNoPrefetchHunk, std::memory_order_release);

    hunk_bytes_ = hunk_bytes;
    hunk_count_ = hunk_count;
    depth_ = depth;

    const std::uint32_t avail = static_cast<std::uint32_t>(depth_) - kPrefetchLookBehind;
    forward_span_ = avail < kPrefetchForwardSpan ? avail : kPrefetchForwardSpan;
    src_ = std::move(src);
    next_ = 0;
    tracking_ = kNoPrefetchHunk;

    if (!chan_.set_live(static_cast<std::uint8_t>(depth_))) {
        depth_ = 0;
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    }
    for (std::size_t i = 0; i < depth_; ++i)
        chan_.send(chan_.acquire());

    park_req_.store(false, std::memory_order_release);
    parked_.store(false, std::memory_order_release);
    wake_.kick();
    return {};
}

void ChdPrefetch::release_media_() noexcept {

    held_ = Chan::Job{};
    src_.reset();
    tracking_ = kNoPrefetchHunk;
    next_ = 0;
    disabled_ = false;
}

void ChdPrefetch::serve() noexcept {

    TASTY_SEAT_BODY_MEDIATOR(ChdPrefetch, A);
    rests_ = false;
    if (park_req_.load(std::memory_order_acquire) && !parked()) {
        release_media_();
        parked_.store(true, std::memory_order_release);
    }
    if (parked()) {
        rests_ = true;
        rest_parked_ = true;
        rest_ms_ = kParkedPollMs;
        return;
    }

    const std::uint32_t want = want_hunk_.load(std::memory_order_acquire);
    if (want != kNoPrefetchHunk && want != tracking_) {
        if (tracking_ == kNoPrefetchHunk) {

            tracking_ = want;
            next_ = want;
        } else if (want > next_ || (want < tracking_ && (tracking_ - want) > kPrefetchLookBehind)) {

            tracking_ = want;
            next_ = want;
            seeks_.fetch_add(1, std::memory_order_relaxed);
        } else {

            tracking_ = want;
        }
    }

    const std::uint32_t lead = next_ - tracking_;
    const bool no_work = disabled_ || (src_ == nullptr) || (want == kNoPrefetchHunk) ||
                         (next_ >= hunk_count_) || (lead >= forward_span_);

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

    const std::uint32_t h = next_;
    std::byte* dst = slab_.get() + static_cast<std::size_t>(job.index()) * hunk_bytes_;
    const std::int64_t t0 = decode_now_ns();
    auto r = src_->read_hunk(h, std::span<std::byte>(dst, hunk_bytes_));
    decode_us_.fetch_add(static_cast<std::uint32_t>((decode_now_ns() - t0) / 1000),
                         std::memory_order_relaxed);
    if (!r) {
        errors_.fetch_add(1, std::memory_order_relaxed);

        held_ = std::move(job);
        disabled_ = true;
        return;
    }
    decodes_.fetch_add(1, std::memory_order_relaxed);
    job->hunk = h;
    job.complete();
    job = Chan::Job{};
    ++next_;

    if (!sprinting_()) rest_(kPrefetchTopUpMs, want, false);
}

void ChdPrefetch::release() noexcept {
    release_media_();
    parked_.store(true, std::memory_order_release);
}

void ChdPrefetch::park_now() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(ChdPrefetch, A);
    if (parked()) return;
    release_media_();
    parked_.store(true, std::memory_order_release);
}

void ChdPrefetch::submit(std::uint32_t hunk) noexcept {
    if (depth_ == 0 || poisoned_) return;
    if (hunk == want_) return;
    want_ = hunk;
    want_hunk_.store(hunk, std::memory_order_release);
    kick_worker_();
}

void ChdPrefetch::kick_worker_() noexcept { wake_.kick_if_armed(); }

std::size_t ChdPrefetch::evict_(std::uint32_t current) noexcept {

    const std::uint32_t lo = current > kPrefetchLookBehind ? current - kPrefetchLookBehind : 0u;
    std::size_t w = 0;
    std::size_t released = 0;
    for (std::size_t i = 0; i < resident_count_; ++i) {
        const std::uint32_t h = resident_hunk_[i];
        const bool in_window = (h >= lo) && (h - lo) < static_cast<std::uint32_t>(depth_);
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
        evictions_.fetch_add(static_cast<std::uint32_t>(released), std::memory_order_relaxed);
    return released;
}

const std::byte* ChdPrefetch::take(std::uint32_t hunk) noexcept {
    if (depth_ == 0 || poisoned_) return nullptr;

    bool kicked = false;
    if (hunk != want_) {
        want_ = hunk;
        want_hunk_.store(hunk, std::memory_order_release);
        kick_worker_();
        kicked = true;
    }

    for (;;) {
        auto l = chan_.reap();
        if (!l) break;

        if (!l.completed() || resident_count_ >= depth_) {
            chan_.send(std::move(l));
            continue;
        }
        resident_hunk_[resident_count_] = l->hunk;
        resident_[resident_count_++] = std::move(l);
    }

    if (evict_(hunk) != 0 && !kicked) kick_worker_();

    for (std::size_t i = 0; i < resident_count_; ++i) {
        if (resident_hunk_[i] == hunk) {
            hits_.fetch_add(1, std::memory_order_relaxed);
            return slab_.get() +
                   static_cast<std::size_t>(chan_.index_of(resident_[i])) * hunk_bytes_;
        }
    }
    return nullptr;
}

PrefetchCounters ChdPrefetch::counters() const noexcept {
    PrefetchCounters c;
    c.hits = hits_.load(std::memory_order_relaxed);
    c.misses = misses_.load(std::memory_order_relaxed);
    c.decodes = decodes_.load(std::memory_order_relaxed);
    c.seeks = seeks_.load(std::memory_order_relaxed);

    c.drops = chan_.census().breaches;
    c.errors = errors_.load(std::memory_order_relaxed);
    c.state = !opened() ? 0u : (parked() ? 1u : 2u);
    c.decode_us = decode_us_.load(std::memory_order_relaxed);
    c.evictions = evictions_.load(std::memory_order_relaxed);
    return c;
}

}  // namespace mister::svc
