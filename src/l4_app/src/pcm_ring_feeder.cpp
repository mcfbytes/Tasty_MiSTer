// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/pcm_ring_feeder.h"

#include <cstring>
#include <limits>
#include <utility>

#include "cores/pcm_source_factory.h"

namespace mister::app {

namespace {
constexpr std::size_t kAlignMask = static_cast<std::size_t>(kPcmSampleAlign) - 1u;
}

void PcmRingFeeder::wake_main_() noexcept {
    wakes_.add();
    commands_.kick_if_armed();
}

void PcmRingFeeder::request_park() noexcept {

    Park cur = park_.load(std::memory_order_acquire);
    while (cur != Park::Owed
           && cur != Park::Releasing
           && !park_.compare_exchange_weak(
               cur,
               Park::Owed,
               std::memory_order_acq_rel,
               std::memory_order_acquire
           )) {
    }
    wake_main_();
}

void PcmRingFeeder::request_swap_() noexcept {
    Park idle = Park::Idle;
    if (park_.compare_exchange_strong(
            idle,
            Park::SwapOwed,
            std::memory_order_acq_rel,
            std::memory_order_relaxed
        )) {
        wake_main_();
    }
}

bool PcmRingFeeder::ready_for_source() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, B);
    if (!parked()) {
        request_swap_();
        return false;
    }

    return park_.load(std::memory_order_acquire) == Park::Idle && reap_media_();
}

Ex<void> PcmRingFeeder::adopt_ring(hal::FpgaMemory&& ring) noexcept {

    const std::size_t len = ring.region().len;

    if (len == 0 || (len & (len - 1)) != 0 || len > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(len)}
        );
    }

    if (!parked() || !reap_media_()) {
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    }

    Park cur = park_.load(std::memory_order_acquire);
    while (cur == Park::Owed || cur == Park::SwapOwed) {
        if (park_.compare_exchange_weak(
                cur,
                Park::Idle,
                std::memory_order_acq_rel,
                std::memory_order_acquire
            )) {
            cur = Park::Idle;
        }
    }
    if (cur != Park::Idle)
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});

    PcmMedia m{};
    m.ring.emplace(std::move(ring));
    const std::uint32_t before = visible_len_.load(std::memory_order_acquire);
    visible_len_.store(static_cast<std::uint32_t>(len), std::memory_order_release);
    if (auto r = hand_over_<PcmCommand::AttachRing>(m); !r) {
        visible_len_.store(before, std::memory_order_release);
        ring = std::move(*m.ring);
        return r;
    }

    return {};
}

Ex<void> PcmRingFeeder::set_sheet(std::string_view sheet) {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, B);

    if (!parked()) {
        request_swap_();
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    }

    if (park_.load(std::memory_order_acquire) != Park::Idle) {
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    }

    if (sheet.empty()) {
        PcmMedia none{};
        return hand_over_<PcmCommand::AttachSource>(none);
    }
    if (visible_len_.load(std::memory_order_acquire) == 0) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    PcmMedia m{};
    if (!m.sheet.assign(sheet))
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    if (auto r = hand_over_<PcmCommand::AttachSource>(m); !r)
        return r;

    parked_.store(false, std::memory_order_release);

    wakes_.add();
    commands_.kick();
    return {};
}

Ex<void> PcmRingFeeder::attach(hal::FpgaMemory ring, std::string_view sheet) noexcept {
    if (sheet.empty())
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    if (auto r = adopt_ring(std::move(ring)); !r)
        return r;
    return set_sheet(sheet);
}

void PcmRingFeeder::detach() noexcept {

    request_park();
}

void PcmRingFeeder::release_media_() noexcept {
    drop_ring_();
    visible_len_.store(0, std::memory_order_release);
}

void PcmRingFeeder::drop_ring_() noexcept {
    release_source_();
    ring_.reset();
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

bool PcmRingFeeder::reap_media_() noexcept {
    while (Media::Loan back = media_.reap()) {
    }
    return media_.has_free();
}

template <class Alt>
Ex<void> PcmRingFeeder::hand_over_(PcmMedia& media) noexcept {
    (void)reap_media_();
    Media::Loan loan = media_.acquire();
    if (!loan)
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});

    if (commands_.size() >= kPcmCommandDepth) {
        drops_.add();
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    }
    const xthread::PoolSlot slot = loan.slot();
    *loan = std::move(media);
    media_.send(std::move(loan));

    if (!commands_.push(infra::make<PcmCommand>(Alt{slot}))) {
        refused_markers_.add();
        return std::unexpected(Error{Errc::busy, ERR_SITE(), static_cast<std::uint32_t>(slot.v)});
    }
    return {};
}

Ex<void> PcmRingFeeder::submit_(const PcmCommand& c) noexcept {
    if (parked() || park_.load(std::memory_order_acquire) != Park::Idle) {
        discards_.add();
        return {};
    }
    if (!commands_.push(c)) {
        drops_.add();
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    }
    wakes_.add();
    return {};
}

Ex<void> PcmRingFeeder::play(svc::TrackNumber track, bool loop) {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, B);
    return submit_(infra::make<PcmCommand>(PcmCommand::Play{track.v, loop}));
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

    if (ever_observed_ && off == last_observed_)
        return;
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

void PcmRingFeeder::publish() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, B);
}

std::size_t PcmRingFeeder::size() const noexcept {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, B);
    return visible_len_.load(std::memory_order_acquire);
}

infra::OptRef<cores::IPcmFeed> PcmRingFeeder::pcm_feed() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, B);
    return infra::OptRef<cores::IPcmFeed>{*this};
}

bool PcmRingFeeder::take_park_ask() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, A);
    Park cur = park_.load(std::memory_order_acquire);
    while (cur == Park::Owed || cur == Park::SwapOwed) {
        const Park next = cur == Park::Owed ? Park::Releasing : Park::SwapReleasing;
        if (!park_.compare_exchange_weak(
                cur,
                next,
                std::memory_order_acq_rel,
                std::memory_order_acquire
            )) {
            continue;
        }
        work_.swap_taken = next == Park::SwapReleasing;
        return true;
    }
    return false;
}

void PcmRingFeeder::finish_park() noexcept {

    if (work_.swap_taken) {
        release_source_();
    } else {
        release_media_();
    }
    publish_state_();
    parked_.store(true, std::memory_order_release);
    Park cur = park_.load(std::memory_order_acquire);
    while ((cur == Park::Releasing || cur == Park::SwapReleasing)
           && !park_.compare_exchange_weak(
               cur,
               Park::Idle,
               std::memory_order_acq_rel,
               std::memory_order_acquire
           )) {
    }
}

void PcmRingFeeder::on(const PcmCommand::Play& a) noexcept {
    apply_play_(a.track, a.loop);
}

void PcmRingFeeder::on(const PcmCommand::Stop&) noexcept {
    ++served_;
    paused_ = true;
}

void PcmRingFeeder::on(const PcmCommand::Resume&) noexcept {
    ++served_;
    paused_ = false;
}

void PcmRingFeeder::on(const PcmCommand::AttachRing& a) noexcept {
    std::optional<PcmMedia> m = take_media_(a.lease);
    if (!m)
        return;
    if (m->ring.has_value()) {
        drop_ring_();
        rp_seen_ = 0;
        read_val_ = 0;
        ring_.swap(m->ring);
        ring_len_ = static_cast<std::uint32_t>(ring_->region().len);
        ring_mask_ = ring_len_ - 1u;
    }
    publish_state_();
}

void PcmRingFeeder::on(const PcmCommand::AttachSource& a) noexcept {
    std::optional<PcmMedia> m = take_media_(a.lease);
    if (!m)
        return;
    release_source_();
    rp_seen_ = 0;
    read_val_ = 0;
    if (!m->sheet.empty() && vfs_) {
        src_ = cores::make_pcm_source(m->sheet.view(), *vfs_);
    }
    publish_state_();
}

std::optional<PcmMedia> PcmRingFeeder::take_media_(xthread::PoolSlot announced) noexcept {
    Media::Job job = media_.take();
    if (!job) {
        misrouted_.add();
        return std::nullopt;
    }
    PcmMedia media{};
    media.ring.swap(job->ring);
    media.sheet = job->sheet;
    if (job.slot() != announced) {
        misrouted_.add();
        return std::nullopt;
    }
    job.complete();
    return media;
}

bool PcmRingFeeder::returned_media_hold_no_ring_for_test() noexcept {
    bool empty = true;
    while (Media::Loan back = media_.reap()) {
        if (back->ring.has_value())
            empty = false;
    }
    return empty;
}

void PcmRingFeeder::misrouted(const PcmCommand&) noexcept {
    misrouted_.add();
}

void PcmRingFeeder::fill_pass() noexcept {
    refresh_read_point_();
    fill_();
    publish_state_();
}

bool PcmRingFeeder::rest() const noexcept {
    const Park p = park_.load(std::memory_order_acquire);
    if (p == Park::Owed || p == Park::SwapOwed)
        return false;
    if (parked())
        return true;
    return read_point_.generation() == rp_seen_;
}

infra::ParkTimeout PcmRingFeeder::rest_ms() const noexcept {
    if (parked())
        return kPcmParkedPollMs;
    return draining_ ? infra::ParkTimeout{5} : kPcmIdlePollMs;
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
    if (target > extent_.bytes)
        target = extent_.bytes;
    return static_cast<std::uint32_t>(target & ~static_cast<std::uint64_t>(kAlignMask));
}

void PcmRingFeeder::apply_play_(std::uint8_t track, [[maybe_unused]] bool loop) noexcept {

    ++served_;
    if (src_ != nullptr)
        src_->close();
    paused_ = false;
    draining_ = false;
    drained_ = false;
    primed_ = false;
    playing_ = false;
    extent_ = cores::PcmExtent{};
    position_ = 0;
    wr_ = 0;

    if (src_ != nullptr && ring_.has_value()) {

        const Ex<cores::PcmExtent> ex = src_->open(svc::TrackNumber{track});
        if (ex) {
            extent_ = *ex;

            const std::span<std::byte> all = ring_->view(0, ring_len_);
            std::memset(all.data(), 0, all.size());
            ring_->publish();

            const std::uint32_t target = prefill_target_();
            std::uint32_t filled = 0;
            while (filled < target) {
                std::uint32_t chunk = target - filled;
                if (chunk > kPcmChunkBytes)
                    chunk = kPcmChunkBytes;
                const Ex<std::span<std::byte>> got = src_->read_at(
                    filled,
                    std::span<std::byte>{staging_, chunk}
                );
                if (!got || got->empty())
                    break;
                const std::size_t n = got->size() & ~kAlignMask;
                if (n == 0)
                    break;
                if (!ring_->write_at(filled, std::span<const std::byte>{staging_, n}))
                    break;
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
    if (first > n)
        first = n;
    if (!ring_->write_at(wr_, std::span<const std::byte>{staging_, first}))
        return 0;
    if (n > first) {
        if (!ring_->write_at(0, std::span<const std::byte>{staging_ + first, n - first})) {
            return first;
        }
    }
    return n;
}

void PcmRingFeeder::refresh_read_point_() noexcept {
    const std::uint32_t g = read_point_.generation();
    if (g == rp_seen_)
        return;
    const auto s = read_point_.sample();
    if (!s)
        return;
    read_val_ = s.value;
    rp_seen_ = s.generation;
}

void PcmRingFeeder::fill_() noexcept {
    if (!playing_ || paused_ || src_ == nullptr || !ring_.has_value())
        return;

    const std::chrono::nanoseconds now = clock_.now().time_since_epoch();
    if (draining_) {
        if (now >= drain_until_) {
            playing_ = false;
            draining_ = false;
            drained_ = true;
        }
        return;
    }

    const std::uint32_t space = (read_val_ - wr_ - 1u) & ring_mask_;
    if (space < kPcmWatermarkBytes)
        return;
    std::uint32_t to_write = space < kPcmChunkBytes ? space : kPcmChunkBytes;

    std::uint64_t remaining = extent_.bytes - position_;
    if (remaining == 0) {
        if (extent_.loops) {
            std::uint64_t loop_byte = extent_.loop_byte;
            if (loop_byte >= extent_.bytes)
                loop_byte = 0;
            position_ = loop_byte;
            remaining = extent_.bytes - loop_byte;
        } else {
            draining_ = true;
            drain_until_ = now + kPcmDrainMs;
            return;
        }
    }
    if (to_write > remaining)
        to_write = static_cast<std::uint32_t>(remaining);
    to_write &= ~static_cast<std::uint32_t>(kAlignMask);
    if (to_write == 0)
        return;

    const Ex<std::span<std::byte>> got = src_->read_at(
        position_,
        std::span<std::byte>{staging_, to_write}
    );
    if (!got || got->empty())
        return;
    std::size_t n = got->size() & ~kAlignMask;
    if (n == 0)
        return;

    n = write_ring_(n);
    if (n == 0)
        return;
    wr_ = static_cast<std::uint32_t>((wr_ + n) & ring_mask_);
    position_ += n;
    passes_.add();
}

void PcmRingFeeder::publish_state_() noexcept {
    cores::PcmFeedState s{};
    s.write_point = wr_;
    s.playing = playing_;
    s.primed = primed_;
    s.drained = drained_;
    s.served = served_;
    if (s == published_ && state_.generation() != 0)
        return;
    published_ = s;
    state_.publish(s);
}

}
