// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/pcm_ring_feeder.h"

#include <cstring>
#include <utility>

namespace mister::app {

namespace {
constexpr std::size_t kAlignMask = static_cast<std::size_t>(kPcmSampleAlign) - 1u;
}

void PcmRingFeeder::wake_main_() noexcept {
    wakes_.fetch_add(1, std::memory_order_relaxed);
    if (commands_ != nullptr) commands_->kick_if_armed();
}

void PcmRingFeeder::request_park() noexcept {

    Park cur = park_.load(std::memory_order_acquire);
    while (cur != Park::Owed && cur != Park::Releasing &&
           !park_.compare_exchange_weak(cur, Park::Owed, std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
    }
    wake_main_();
}

void PcmRingFeeder::request_swap_() noexcept {
    Park idle = Park::Idle;
    if (park_.compare_exchange_strong(idle, Park::SwapOwed, std::memory_order_acq_rel,
                                      std::memory_order_relaxed)) {
        wake_main_();
    }
}

bool PcmRingFeeder::ready_for_source() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, B);
    if (!parked()) {
        request_swap_();
        return false;
    }
    return park_.load(std::memory_order_acquire) == Park::Idle;
}

Ex<void> PcmRingFeeder::attach_ring(hal::FpgaMemory ring) noexcept {

    const std::size_t len = ring.region().len;

    if (len == 0 || (len & (len - 1)) != 0 || len > 0xFFFF'FFFFu) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(len)});
    }
    if (commands_ == nullptr) return std::unexpected(Error{Errc::io, ERR_SITE(), 0});

    if (!parked()) return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});

    Park cur = park_.load(std::memory_order_acquire);
    while (cur == Park::Owed || cur == Park::SwapOwed) {
        if (park_.compare_exchange_weak(cur, Park::Idle, std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
            cur = Park::Idle;
        }
    }
    if (cur != Park::Idle) return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});

    release_media_();
    rp_seen_ = 0;
    read_val_ = 0;
    ring_.emplace(std::move(ring));
    ring_len_ = static_cast<std::uint32_t>(len);
    ring_mask_ = ring_len_ - 1u;
    visible_len_.store(ring_len_, std::memory_order_release);

    return {};
}

Ex<void> PcmRingFeeder::set_source(std::unique_ptr<cores::IPcmSource> src) {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, B);

    if (!parked()) {
        request_swap_();
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    }

    if (park_.load(std::memory_order_acquire) != Park::Idle) {
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    }

    src_.reset();
    reset_playback_();
    rp_seen_ = 0;
    read_val_ = 0;
    if (src == nullptr) return {};
    if (!ring_.has_value()) return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    if (commands_ == nullptr) return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    src_ = std::move(src);

    parked_.store(false, std::memory_order_release);

    wakes_.fetch_add(1, std::memory_order_relaxed);
    commands_->kick();
    return {};
}

Ex<void> PcmRingFeeder::attach(hal::FpgaMemory ring,
                               std::unique_ptr<cores::IPcmSource> src) noexcept {
    if (!src) return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    if (auto r = attach_ring(std::move(ring)); !r) return r;
    return set_source(std::move(src));
}

void PcmRingFeeder::detach() noexcept { request_park(); }

void PcmRingFeeder::release_media_() noexcept {
    release_source_();
    ring_.reset();
    visible_len_.store(0, std::memory_order_release);
    ring_len_ = 0;
    ring_mask_ = 0;
}

void PcmRingFeeder::release_source_() noexcept {
    src_.reset();
    reset_playback_();
}

void PcmRingFeeder::reset_playback_() noexcept {
    wr_ = 0;
    position_ = 0;
    extent_ = cores::PcmExtent{};
    playing_ = false;
    paused_ = false;
    draining_ = false;
    primed_ = false;
    drained_ = false;
}

Ex<void> PcmRingFeeder::submit_(const PcmCommand& c) noexcept {
    if (parked() || park_.load(std::memory_order_acquire) != Park::Idle) {
        discards_.fetch_add(1, std::memory_order_relaxed);
        return {};
    }
    if (commands_ == nullptr || !commands_->push(c)) {
        drops_.fetch_add(1, std::memory_order_relaxed);
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    }
    wakes_.fetch_add(1, std::memory_order_relaxed);
    return {};
}

Ex<void> PcmRingFeeder::play(std::uint8_t track, bool loop) {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, B);
    return submit_(infra::make<PcmCommand>(PcmCommand::Play{track, loop}));
}

Ex<void> PcmRingFeeder::stop() {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, B);
    return submit_(infra::make<PcmCommand>(PcmCommand::Stop{}));
}

Ex<void> PcmRingFeeder::resume() {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, B);
    return submit_(infra::make<PcmCommand>(PcmCommand::Resume{}));
}

void PcmRingFeeder::observe_read_point(std::uint32_t off) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, B);

    if (ever_observed_ && off == last_observed_) return;
    last_observed_ = off;
    ever_observed_ = true;
    read_point_.publish(off);
    wake_main_();
}

cores::PcmFeedState PcmRingFeeder::state() const noexcept {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, B);
    return state_.sample().value;
}

bool PcmRingFeeder::can_serve() const noexcept {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, B);

    return !parked() && attached();
}

Ex<std::size_t> PcmRingFeeder::write(std::size_t, std::span<const std::byte>) {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, B);
    return unimplemented(ERR_SITE());
}

Ex<std::size_t> PcmRingFeeder::read(std::size_t, std::span<std::byte>) const {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, B);
    return unimplemented(ERR_SITE());
}

void PcmRingFeeder::publish() noexcept { TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, B); }

std::size_t PcmRingFeeder::size() const noexcept {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, B);
    return visible_len_.load(std::memory_order_acquire);
}

cores::IPcmFeed* PcmRingFeeder::pcm_feed() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, B);
    return this;
}

bool PcmRingFeeder::take_park_ask() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, A);
    Park cur = park_.load(std::memory_order_acquire);
    while (cur == Park::Owed || cur == Park::SwapOwed) {
        const Park next = cur == Park::Owed ? Park::Releasing : Park::SwapReleasing;
        if (!park_.compare_exchange_weak(cur, next, std::memory_order_acq_rel,
                                         std::memory_order_acquire)) {
            continue;
        }

        if (next == Park::Releasing) {
            release_media_();
        } else {
            release_source_();
        }
        return true;
    }
    return false;
}

void PcmRingFeeder::finish_park() noexcept {
    publish_state_();
    parked_.store(true, std::memory_order_release);
    Park cur = park_.load(std::memory_order_acquire);
    while ((cur == Park::Releasing || cur == Park::SwapReleasing) &&
           !park_.compare_exchange_weak(cur, Park::Idle, std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
    }
}

void PcmRingFeeder::on(const PcmCommand::Play& a) noexcept { apply_play_(a.track, a.loop); }

void PcmRingFeeder::on(const PcmCommand::Stop&) noexcept {
    ++served_;
    paused_ = true;
}

void PcmRingFeeder::on(const PcmCommand::Resume&) noexcept {
    ++served_;
    paused_ = false;
}

void PcmRingFeeder::misrouted(const PcmCommand&) noexcept {
    misrouted_.fetch_add(1, std::memory_order_relaxed);
}

void PcmRingFeeder::fill_pass() noexcept {
    refresh_read_point_();
    fill_();
    publish_state_();
}

bool PcmRingFeeder::rest() const noexcept {
    const Park p = park_.load(std::memory_order_acquire);
    if (p == Park::Owed || p == Park::SwapOwed) return false;
    if (parked()) return true;
    return read_point_.generation() == rp_seen_;
}

int PcmRingFeeder::rest_ms() const noexcept {
    if (parked()) return kPcmParkedPollMs;
    return draining_ ? 5 : kPcmIdlePollMs;
}

void PcmRingFeeder::release() noexcept {
    release_media_();
    parked_.store(true, std::memory_order_release);
}

void PcmRingFeeder::park_now() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, A);
    release_media_();
    publish_state_();
    parked_.store(true, std::memory_order_release);
    park_.store(Park::Idle, std::memory_order_release);
}

std::uint32_t PcmRingFeeder::prefill_target_() const noexcept {
    std::uint64_t target = ring_len_ / 2u;
    if (target > extent_.bytes) target = extent_.bytes;
    return static_cast<std::uint32_t>(target & ~static_cast<std::uint64_t>(kAlignMask));
}

void PcmRingFeeder::apply_play_(std::uint8_t track, [[maybe_unused]] bool loop) noexcept {

    ++served_;
    if (src_ != nullptr) src_->close();
    paused_ = false;
    draining_ = false;
    drained_ = false;
    primed_ = false;
    playing_ = false;
    extent_ = cores::PcmExtent{};
    position_ = 0;
    wr_ = 0;

    if (src_ != nullptr && ring_.has_value()) {

        const Ex<cores::PcmExtent> ex = src_->open(track);
        if (ex) {
            extent_ = *ex;

            const std::span<std::byte> all = ring_->view(0, ring_len_);
            std::memset(all.data(), 0, all.size());
            ring_->publish();

            const std::uint32_t target = prefill_target_();
            std::uint32_t filled = 0;
            while (filled < target) {
                std::uint32_t chunk = target - filled;
                if (chunk > kPcmChunkBytes) chunk = kPcmChunkBytes;
                const Ex<std::size_t> got =
                    src_->read_at(filled, std::span<std::byte>{staging_, chunk});
                if (!got || *got == 0) break;
                const std::size_t n = *got & ~kAlignMask;
                if (n == 0) break;
                if (!ring_->write_at(filled, std::span<const std::byte>{staging_, n})) break;
                filled += static_cast<std::uint32_t>(n);
            }
            wr_ = filled & ring_mask_;
            position_ = filled;
            playing_ = true;
        }
    }

    primed_ = true;
}

std::size_t PcmRingFeeder::write_ring_(std::size_t n) noexcept {
    std::size_t first = ring_len_ - wr_;
    if (first > n) first = n;
    if (!ring_->write_at(wr_, std::span<const std::byte>{staging_, first})) return 0;
    if (n > first) {
        if (!ring_->write_at(0, std::span<const std::byte>{staging_ + first, n - first})) {
            return first;
        }
    }
    return n;
}

void PcmRingFeeder::refresh_read_point_() noexcept {
    const std::uint32_t g = read_point_.generation();
    if (g == rp_seen_) return;
    std::uint32_t v = 0;
    const std::uint32_t got = read_point_.sample_into(v);
    if (got == 0) return;
    read_val_ = v;
    rp_seen_ = got;
}

void PcmRingFeeder::fill_() noexcept {
    if (!playing_ || paused_ || src_ == nullptr || !ring_.has_value()) return;

    const std::chrono::nanoseconds now = clock_->now();
    if (draining_) {
        if (now >= drain_until_) {
            playing_ = false;
            draining_ = false;
            drained_ = true;
        }
        return;
    }

    const std::uint32_t space = (read_val_ - wr_ - 1u) & ring_mask_;
    if (space < kPcmWatermarkBytes) return;
    std::uint32_t to_write = space < kPcmChunkBytes ? space : kPcmChunkBytes;

    std::uint64_t remaining = extent_.bytes - position_;
    if (remaining == 0) {
        if (extent_.loops) {
            std::uint64_t loop_byte = extent_.loop_byte;
            if (loop_byte >= extent_.bytes) loop_byte = 0;
            position_ = loop_byte;
            remaining = extent_.bytes - loop_byte;
        } else {
            draining_ = true;
            drain_until_ = now + kPcmDrainMs;
            return;
        }
    }
    if (to_write > remaining) to_write = static_cast<std::uint32_t>(remaining);
    to_write &= ~static_cast<std::uint32_t>(kAlignMask);
    if (to_write == 0) return;

    const Ex<std::size_t> got = src_->read_at(position_, std::span<std::byte>{staging_, to_write});
    if (!got || *got == 0) return;
    std::size_t n = *got & ~kAlignMask;
    if (n == 0) return;

    n = write_ring_(n);
    if (n == 0) return;
    wr_ = static_cast<std::uint32_t>((wr_ + n) & ring_mask_);
    position_ += n;
    passes_.fetch_add(1, std::memory_order_relaxed);
}

void PcmRingFeeder::publish_state_() noexcept {
    cores::PcmFeedState s{};
    s.write_point = wr_;
    s.playing = playing_;
    s.primed = primed_;
    s.drained = drained_;
    s.served = served_;
    if (s.write_point == published_.write_point && s.playing == published_.playing &&
        s.primed == published_.primed && s.drained == published_.drained &&
        s.served == published_.served && state_.generation() != 0) {
        return;
    }
    published_ = s;
    state_.publish(s);
}

}  // namespace mister::app
