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

    const PcmLatch::Ticket t = latch_.ask(PcmRelease::Media);
    if (t.gen != ticket_.gen)
        wakes_.add();
    ticket_ = t;
}

void PcmRingFeeder::request_swap_() noexcept {
    const PcmLatch::Ticket t = latch_.ask(PcmRelease::Source);
    if (t.gen != ticket_.gen)
        wakes_.add();
    ticket_ = t;
}

bool PcmRingFeeder::ready_for_source() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, B);
    if (source_live_()) {
        request_swap_();
        return false;
    }

    return !latch_.outstanding() && reap_media_();
}

Ex<void> PcmRingFeeder::adopt_ring(hal::FpgaMemory&& ring) noexcept {

    const std::size_t len = ring.region().len;

    if (len == 0 || (len & (len - 1)) != 0 || len > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(len)}
        );
    }

    if (source_live_() || !reap_media_()) {
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    }

    PcmMedia m{};
    m.ring.emplace(std::move(ring));
    m.stamp = latch_.asked();
    if (auto r = hand_over_<PcmCommand::AttachRing>(m); !r) {
        ring = std::move(*m.ring);
        return r;
    }

    latch_.handed_over();
    rings_handed_ = rings_handed_.next();
    handed_len_ = os::MapLength{len};

    return {};
}

Ex<void> PcmRingFeeder::set_sheet(std::string_view sheet) {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, B);

    if (source_live_()) {
        request_swap_();
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    }

    if (latch_.outstanding()) {
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    }

    if (sheet.empty()) {
        PcmMedia none{};
        return hand_over_<PcmCommand::AttachSource>(none);
    }
    if (!attached()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    PcmMedia m{};
    if (!m.sheet.assign(sheet))
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    if (auto r = hand_over_<PcmCommand::AttachSource>(m); !r)
        return r;

    sources_handed_ = sources_handed_.next();

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
}

void PcmRingFeeder::drop_ring_() noexcept {
    release_source_();
    if (ring_.has_value())
        rings_ended_ = rings_ended_.next();
    ring_.reset();
    ring_geom_ = cores::PcmRing{};
    ring_stamp_ = xthread::ReleaseGen::none();
}

void PcmRingFeeder::release_source_() noexcept {
    if (src_ != nullptr)
        sources_ended_ = sources_ended_.next();
    src_.reset();
    reset_playback_();
}

void PcmRingFeeder::reset_playback_() noexcept {
    wr_ = cores::PcmRingPoint{};
    position_ = {};
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
        return std::unexpected(slot.refusal(Errc::busy, ERR_SITE()));
    }
    return {};
}

Ex<void> PcmRingFeeder::submit_(const PcmCommand& c) noexcept {
    if (!source_live_() || latch_.outstanding()) {
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

void PcmRingFeeder::observe_read_point(cores::PcmRingPoint at) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, B);

    if (ever_observed_ && at == last_observed_)
        return;
    last_observed_ = at;
    ever_observed_ = true;
    read_point_.publish(at);
    wake_main_();
}

cores::PcmFeedState PcmRingFeeder::state() const noexcept {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, B);
    return state_.sample().value;
}

bool PcmRingFeeder::can_serve() const noexcept {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, B);

    return source_live_() && attached();
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
    return attached() ? handed_len_.bytes() : 0;
}

infra::OptRef<cores::IPcmFeed> PcmRingFeeder::pcm_feed() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, B);
    return infra::OptRef<cores::IPcmFeed>{*this};
}

bool PcmRingFeeder::take_park_ask() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, A);
    work_.taken = latch_.take();
    return work_.taken.has_value();
}

void PcmRingFeeder::finish_park() noexcept {
    const bool spared = work_.taken.has_value()
        && ring_.has_value()
        && ring_stamp_.covers(work_.taken->gen);
    if (work_.taken.has_value() && (work_.taken->kind == PcmRelease::Source || spared)) {
        release_source_();
    } else {
        release_media_();
    }
    publish_state_();
    parked_.store(true, std::memory_order_release);
    if (work_.taken.has_value()) {
        latch_.done(*work_.taken);
        work_.taken.reset();
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
        read_val_ = cores::PcmRingPoint{};
        ring_.swap(m->ring);
        ring_geom_ = cores::PcmRing::of_length(ring_->region().len);
        ring_stamp_ = m->stamp;
    }
    publish_state_();
}

void PcmRingFeeder::on(const PcmCommand::AttachSource& a) noexcept {
    std::optional<PcmMedia> m = take_media_(a.lease);
    if (!m)
        return;
    release_source_();
    rp_seen_ = 0;
    read_val_ = cores::PcmRingPoint{};
    if (!m->sheet.empty() && vfs_) {
        src_ = cores::make_pcm_source(m->sheet.view(), *vfs_);
    }

    if (!m->sheet.empty() && src_ == nullptr)
        sources_ended_ = sources_ended_.next();

    if (src_ != nullptr)
        parked_.store(false, std::memory_order_release);
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
    media.stamp = job->stamp;
    if (job.slot() != announced) {
        misrouted_.add();

        if (media.ring.has_value())
            rings_ended_ = rings_ended_.next();
        if (!media.sheet.empty())
            sources_ended_ = sources_ended_.next();
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
    if (latch_.pending())
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
    publish_state_();
    parked_.store(true, std::memory_order_release);
    if (work_.taken.has_value()) {
        latch_.done(*work_.taken);
        work_.taken.reset();
    }
}

void PcmRingFeeder::park_now() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(PcmRingFeeder, A);
    release_media_();
    publish_state_();
    parked_.store(true, std::memory_order_release);
    if (work_.taken.has_value()) {
        latch_.done(*work_.taken);
        work_.taken.reset();
    }
}

std::uint32_t PcmRingFeeder::prefill_target_() const noexcept {
    std::uint64_t target = ring_geom_.bytes() / kPrefillParts;
    if (target > extent_.bytes)
        target = extent_.bytes;
    return static_cast<std::uint32_t>(target & ~static_cast<std::uint64_t>(kAlignMask));
}

void PcmRingFeeder::apply_play_(svc::TrackNumber track, [[maybe_unused]] bool loop) noexcept {

    ++served_;
    if (src_ != nullptr)
        src_->close();
    paused_ = false;
    draining_ = false;
    drained_ = false;
    primed_ = false;
    playing_ = false;
    extent_ = cores::PcmExtent{};
    position_ = {};
    wr_ = cores::PcmRingPoint{};

    if (src_ != nullptr && ring_.has_value()) {

        const Ex<cores::PcmExtent> ex = src_->open(track);
        if (ex) {
            extent_ = *ex;

            const std::span<std::byte> all = ring_->view(0, ring_geom_.bytes());
            std::memset(all.data(), 0, all.size());
            ring_->publish();

            const std::uint32_t target = prefill_target_();
            std::uint32_t filled = 0;
            while (filled < target) {
                std::uint32_t chunk = target - filled;
                if (chunk > kPcmChunkBytes)
                    chunk = kPcmChunkBytes;
                const Ex<std::span<std::byte>> got = src_->read_at(
                    proto::FileSize{filled},
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
            wr_ = ring_geom_.advanced(cores::PcmRingPoint{}, filled);
            position_ = proto::FileSize{filled};
            playing_ = true;
        }
    }

    primed_ = true;
}

std::size_t PcmRingFeeder::write_ring_(std::size_t n) noexcept {
    std::size_t first = ring_geom_.run_to_end(wr_);
    if (first > n)
        first = n;
    if (!ring_->write_at(wr_.byte_offset(), std::span<const std::byte>{staging_, first}))
        return 0;
    if (n > first) {
        if (!ring_->write_at(
                cores::PcmRingPoint{}.byte_offset(),
                std::span<const std::byte>{staging_ + first, n - first}
            )) {
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

    const std::uint32_t space = ring_geom_.free_bytes({.write = wr_, .read = read_val_});
    if (space < kPcmWatermarkBytes)
        return;
    std::uint32_t to_write = space < kPcmChunkBytes ? space : kPcmChunkBytes;

    const proto::FileSize total{extent_.bytes};
    proto::FileSize remaining = total - position_;
    if (remaining == proto::FileSize{}) {
        if (extent_.loops) {
            proto::FileSize loop_at{extent_.loop_byte};
            if (loop_at >= total)
                loop_at = {};
            position_ = loop_at;
            remaining = total - loop_at;
        } else {
            draining_ = true;
            drain_until_ = now + kPcmDrainMs;
            return;
        }
    }
    const std::span<std::byte> chunk = proto::FileSize{to_write}.min(remaining).first_of(
        std::span<std::byte>{staging_, to_write}
    );
    const std::span<std::byte> window = chunk.first(chunk.size() & ~kAlignMask);
    if (window.empty())
        return;

    const Ex<std::span<std::byte>> got = src_->read_at(position_, window);
    if (!got || got->empty())
        return;
    std::size_t n = got->size() & ~kAlignMask;
    if (n == 0)
        return;

    n = write_ring_(n);
    if (n == 0)
        return;
    wr_ = ring_geom_.advanced(wr_, n);
    position_ += proto::FileSize{n};
    passes_.add();
}

void PcmRingFeeder::publish_state_() noexcept {
    cores::PcmFeedState s{};
    s.write_point = wr_;
    s.playing = playing_;
    s.primed = primed_;
    s.drained = drained_;
    s.served = served_;
    s.rings_ended = rings_ended_;
    s.sources_ended = sources_ended_;
    if (s == published_ && state_.generation() != 0)
        return;
    published_ = s;
    state_.publish(s);
}

PcmRingFeeder::Ended PcmRingFeeder::sample_ended_() const noexcept {
    const auto s = state_.sample();
    if (s) {
        ended_.rings = s.value.rings_ended;
        ended_.sources = s.value.sources_ended;
    }
    return ended_;
}

bool PcmRingFeeder::source_live_() const noexcept {
    return sources_handed_ != sample_ended_().sources;
}

}
