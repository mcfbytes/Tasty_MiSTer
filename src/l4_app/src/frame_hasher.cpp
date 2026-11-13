// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_hasher.h"

#include <algorithm>

#include "app/log_clock.h"
#include "app/raw_frame_dispatch.h"
#include "infra/message_sum.h"
#include "svc/crc32_accumulator.h"
#include "svc/types.h"

namespace mister::app {

svc::Crc32 FrameHasher::hash_rows(const RawFrameSlot::Frame& s) noexcept {
    svc::Crc32AccumulatorT<seat_owner::Encode> crc;
    if (s.rgb.bytes.empty())
        return crc.value();
    const RowBytes row = rgb_row_of(s.width);
    s.rgb.each_row(row, [&](std::span<const std::byte> line) { crc.extend(line); });
    return crc.value();
}

bool FrameHasher::idle() const noexcept {

    return job_ || drained();
}

bool FrameHasher::drained() const noexcept {
    return !job_ && w_.channel.outbound() == 0;
}

infra::ParkTimeout FrameHasher::park_ms() const noexcept {
    if (job_)
        return kHeldPollMs;
    return w_.video.park_ms();
}

void FrameHasher::release() noexcept {
    job_ = RawFrameChannel::Job{};
    w_.video.release();
}

bool FrameHasher::push_(const SidecarMsg& m) noexcept {
    if (w_.out.push(m))
        return true;
    ++st_.ring_full;
    dirty_ = true;
    return false;
}

bool FrameHasher::push_row_(const SidecarMsg::Row& row, ControlGeneration gen) noexcept {
    if (!push_(infra::make<SidecarMsg>(row, SidecarMsg::Head{gen})))
        return false;
    ++st_.rows;
    return true;
}

template <class A>
bool FrameHasher::chunk_(
    const A& s,
    ControlGeneration gen,
    bool real,
    SidecarMsg::Row& row
) noexcept {
    if (!chunked_) {
        if (!w_.video.chunk(s, gen, real, row.stamp.core_frame, row.stamp.movie_frame))
            return false;
        chunked_ = true;
    }
    row.place = w_.video.place();
    return true;
}

template <class A>
bool FrameHasher::emit_runs_(const A& s, ControlGeneration gen) noexcept {
    SidecarMsg::Row row{};
    row.width = s.width;
    row.height = s.rgb.rows;
    std::uint32_t k = 0;
    for (std::uint8_t r = 0; r < s.runs; ++r) {
        const FrameStampRun& run = s.run[r];
        for (CoreFrameCount i{}; i < run.count; i += CoreFrameCount{1}, ++k) {
            if (k < next_)
                continue;
            row.hash = prev_hash_;
            row.stamp = run.stamp_at(i);
            if (s.stamp.movie_frame >= MovieFrame{}) {
                const WideMovieFrame mf = wide_frame(s.stamp.movie_frame)
                    - (s.stamp.core_frame - row.stamp.core_frame).movie_span();
                row.stamp.movie_frame = std::max(mf, kNoWideMovieFrame).narrowed();
            }
            if (!chunk_(s, gen, false, row) || !push_row_(row, gen))
                return false;
            ++next_;
            chunked_ = false;
        }
    }
    return true;
}

bool FrameHasher::on(const RawFrameSlot::Frame& s, ControlGeneration head) noexcept {
    if (!emit_runs_(s, head))
        return false;
    const bool real = s.stamp.dup == DupReason::None && !s.rgb.bytes.empty();
    if (real && !hashed_) {
        const auto t0 = w_.clock.now();
        cur_hash_ = hash_rows(s);
        hashed_ = true;
        ++st_.frames;
        st_.hash_us.note(us_of(w_.clock.now() - t0));
    }
    SidecarMsg::Row row{};
    row.width = s.width;
    row.height = s.rgb.rows;
    row.hash = real ? cur_hash_ : prev_hash_;
    row.stamp = s.stamp;
    if (!chunk_(s, head, real, row) || !push_row_(row, head))
        return false;

    if (w_.video.place().shows_hash())
        prev_hash_ = row.hash;
    return true;
}

bool FrameHasher::on(const RawFrameSlot::Open& s, ControlGeneration head) noexcept {
    if (!chunked_) {
        if (!w_.video.open(s, head))
            return false;
    }
    chunked_ = true;
    prev_hash_ = {};
    return push_(infra::make<SidecarMsg>(SidecarMsg::Open{.path = s.path}, SidecarMsg::Head{head}));
}

bool FrameHasher::on(const RawFrameSlot::Close& s, ControlGeneration head) noexcept {
    if (!emit_runs_(s, head))
        return false;
    if (!chunked_) {
        if (!w_.video.close(s, head))
            return false;
    }
    chunked_ = true;
    return push_(infra::make<SidecarMsg>(SidecarMsg::Close{}, SidecarMsg::Head{head}));
}

bool FrameHasher::misrouted(const RawFrameSlot&) noexcept {
    TASTY_SEAT_BODY(FrameHasher);
    return true;
}

bool FrameHasher::emit_(const RawFrameSlot& s) noexcept {
    return infra::dispatch<RawFrameRoutes>(s, *this);
}

void FrameHasher::serve() noexcept {
    TASTY_SEAT_BODY(FrameHasher);
    w_.video.tend();
    for (unsigned n = 0; n < kJobsPerPass; ++n) {
        if (!job_) {
            job_ = w_.channel.take();
            next_ = 0;
            hashed_ = false;
            chunked_ = false;
        }
        if (!job_)
            break;
        const bool done = emit_(*job_);
        if (!done) {
            w_.video.note_held();
            break;
        }
        job_.complete();
        job_ = RawFrameChannel::Job{};
        dirty_ = true;
    }
    if (w_.video.take_dirty()) {
        st_.video = w_.video.counts();
        dirty_ = true;
    }
    if (dirty_)
        w_.status.publish(st_);
    dirty_ = false;
}

}
