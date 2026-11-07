// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_hasher.h"

#include <zlib.h>

#include <algorithm>

#include "app/log_clock.h"
#include "infra/byte_view.h"

namespace mister::app {

std::uint32_t FrameHasher::hash_rows(const RawFrameSlot& s) noexcept {
    uLong crc = ::crc32(0L, Z_NULL, 0);
    if (s.rgb.bytes.empty())
        return static_cast<std::uint32_t>(crc);
    const RowBytes row = rgb_row_of(s.width);
    s.rgb.each_row(row, [&](std::span<const std::byte> line) {
        crc = ::crc32(
            crc,
            infra::byte_cast<const std::uint8_t>(line).data(),
            static_cast<uInt>(line.size())
        );
    });
    return static_cast<std::uint32_t>(crc);
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
    if (w_.out.push(m)) {
        if (m.kind == SidecarKind::Row)
            ++st_.rows;
        return true;
    }
    ++st_.ring_full;
    dirty_ = true;
    return false;
}

bool FrameHasher::chunk_(const RawFrameSlot& s, bool real, SidecarMsg& m) noexcept {
    if (!chunked_) {
        if (!w_.video.chunk(s, real, m.stamp.core_frame, m.stamp.movie_frame))
            return false;
        chunked_ = true;
    }
    const AviEncoder::Position p = w_.video.position();
    m.segment = p.segment;
    m.seg_frame = p.lost ? -1 : p.frame;
    return true;
}

bool FrameHasher::emit_runs_(const RawFrameSlot& s, SidecarMsg& m) noexcept {
    m.kind = SidecarKind::Row;
    m.width = s.width;
    m.height = s.rgb.rows;
    std::uint32_t k = 0;
    for (std::uint8_t r = 0; r < s.runs; ++r) {
        const FrameStampRun& run = s.run[r];
        for (CoreFrameCount i{}; i < run.count; i += CoreFrameCount{1}, ++k) {
            if (k < next_)
                continue;
            m.hash = prev_hash_;
            m.stamp = run.stamp_at(i);
            if (s.stamp.movie_frame >= MovieFrame{}) {
                const WideMovieFrame mf = wide_frame(s.stamp.movie_frame)
                    - (s.stamp.core_frame - m.stamp.core_frame).movie_span();
                m.stamp.movie_frame = std::max(mf, kNoWideMovieFrame).narrowed();
            }
            if (!chunk_(s, false, m) || !push_(m))
                return false;
            ++next_;
            chunked_ = false;
        }
    }
    return true;
}

bool FrameHasher::emit_(const RawFrameSlot& s) noexcept {
    SidecarMsg m{};
    m.gen = s.gen;
    if (s.kind == RawKind::Close && !emit_runs_(s, m))
        return false;
    if (s.kind != RawKind::Frame) {
        if (!chunked_) {
            if (!(s.kind == RawKind::Open ? w_.video.open(s) : w_.video.close(s)))
                return false;
        }
        chunked_ = true;
        m.kind = s.kind == RawKind::Open ? SidecarKind::Open : SidecarKind::Close;
        m.path = s.path;
        if (s.kind == RawKind::Open)
            prev_hash_ = 0;
        return push_(m);
    }
    if (!emit_runs_(s, m))
        return false;
    const bool real = s.stamp.dup == DupReason::None && !s.rgb.bytes.empty();
    if (real && !hashed_) {
        const std::chrono::nanoseconds t0 = w_.clock.now();
        cur_hash_ = hash_rows(s);
        hashed_ = true;
        ++st_.frames;
        st_.hash_us.note(us_of(w_.clock.now() - t0));
    }
    m.stamp = s.stamp;
    m.hash = real ? cur_hash_ : prev_hash_;
    if (!chunk_(s, real, m) || !push_(m))
        return false;

    if (!w_.video.position().lost)
        prev_hash_ = m.hash;
    return true;
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
        w_.writer_wake.kick_if_armed();
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
