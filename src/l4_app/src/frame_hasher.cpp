// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_hasher.h"

#include <zlib.h>

#include <algorithm>

namespace mister::app {

std::uint32_t FrameHasher::hash_rows(const RawFrameSlot& s) noexcept {
    uLong crc = ::crc32(0L, Z_NULL, 0);
    if (s.pixels == nullptr) return static_cast<std::uint32_t>(crc);
    const std::size_t row = static_cast<std::size_t>(s.width) * 3u;
    const auto* p = reinterpret_cast<const Bytef*>(s.pixels);
    for (std::uint32_t y = 0; y < s.height; ++y)
        crc = ::crc32(crc, p + static_cast<std::size_t>(y) * s.line, static_cast<uInt>(row));
    return static_cast<std::uint32_t>(crc);
}

bool FrameHasher::idle() const noexcept { return job_ || drained(); }

bool FrameHasher::drained() const noexcept {
    return !job_ && (w_.channel == nullptr || w_.channel->outbound() == 0);
}

int FrameHasher::park_ms() const noexcept {
    if (job_) return kHeldPollMs;
    return w_.video != nullptr ? w_.video->park_ms() : -1;
}

void FrameHasher::release() noexcept {
    job_ = RawFrameChannel::Job{};
    if (w_.video != nullptr) w_.video->release();
}

bool FrameHasher::push_(const SidecarMsg& m) noexcept {
    if (w_.out->push(m)) {
        if (m.kind == SidecarKind::Row) ++st_.rows;
        return true;
    }
    ++st_.ring_full;
    dirty_ = true;
    return false;
}

bool FrameHasher::chunk_(const RawFrameSlot& s, bool real, SidecarMsg& m) noexcept {
    if (w_.video == nullptr) return true;
    if (!chunked_) {
        if (!w_.video->chunk(s, real)) return false;
        chunked_ = true;
    }
    const AviEncoder::Position p = w_.video->position();
    m.segment = p.segment;
    m.seg_frame = p.lost ? -1 : p.frame;
    return true;
}

bool FrameHasher::emit_(const RawFrameSlot& s) noexcept {
    SidecarMsg m{};
    m.gen = s.gen;
    if (s.kind != RawKind::Frame) {
        if (!chunked_ && w_.video != nullptr) {
            if (!(s.kind == RawKind::Open ? w_.video->open(s) : w_.video->close(s))) return false;
        }
        chunked_ = true;
        m.kind = s.kind == RawKind::Open ? SidecarKind::Open : SidecarKind::Close;
        m.path = s.path;
        if (s.kind == RawKind::Open) prev_hash_ = 0;
        return push_(m);
    }
    m.kind = SidecarKind::Row;
    m.width = s.width;
    m.height = s.height;
    std::uint32_t k = 0;
    for (std::uint8_t r = 0; r < s.runs; ++r) {
        const FrameStampRun& run = s.run[r];
        for (std::uint32_t i = 0; i < run.count; ++i, ++k) {
            if (k < next_) continue;
            m.hash = prev_hash_;
            m.stamp = FrameStamp{};
            m.stamp.core_frame = run.first + i;
            m.stamp.header_ctr = static_cast<std::uint8_t>((run.first_ctr + i) & 0x7u);
            m.stamp.dup = run.why;
            if (s.stamp.movie_frame >= 0) {
                const std::int64_t mf =
                    static_cast<std::int64_t>(s.stamp.movie_frame) -
                    static_cast<std::int64_t>(s.stamp.core_frame - m.stamp.core_frame);
                m.stamp.movie_frame = static_cast<std::int32_t>(std::max<std::int64_t>(mf, -1));
            }
            if (!chunk_(s, false, m) || !push_(m)) return false;
            ++next_;
            chunked_ = false;
        }
    }
    const bool real = s.stamp.dup == DupReason::None && s.pixels != nullptr;
    if (real && !hashed_) {
        const std::int64_t t0 = w_.clock != nullptr ? w_.clock->now().count() : 0;
        cur_hash_ = hash_rows(s);
        hashed_ = true;
        ++st_.frames;
        if (w_.clock != nullptr) {
            st_.hash_us_last = static_cast<std::uint32_t>((w_.clock->now().count() - t0) / 1000);
            st_.hash_us_max = std::max(st_.hash_us_max, st_.hash_us_last);
        }
    }
    m.stamp = s.stamp;
    m.hash = real ? cur_hash_ : prev_hash_;
    if (!chunk_(s, real, m) || !push_(m)) return false;

    if (w_.video == nullptr || !w_.video->position().lost) prev_hash_ = m.hash;
    return true;
}

void FrameHasher::serve() noexcept {
    TASTY_SEAT_BODY(FrameHasher);
    if (w_.channel == nullptr || w_.out == nullptr) return;
    if (w_.video != nullptr) w_.video->tend();
    for (unsigned n = 0; n < kJobsPerPass; ++n) {
        if (!job_) {
            job_ = w_.channel->take();
            next_ = 0;
            hashed_ = false;
            chunked_ = false;
        }
        if (!job_) break;
        const bool done = emit_(*job_);
        if (w_.writer_wake != nullptr) w_.writer_wake->kick_if_armed();
        if (!done) {
            if (w_.video != nullptr) w_.video->note_held();
            break;
        }
        job_.complete();
        job_ = RawFrameChannel::Job{};
        dirty_ = true;
    }
    if (w_.video != nullptr && w_.video->take_dirty()) {
        st_.video = w_.video->counts();
        dirty_ = true;
    }
    if (dirty_ && w_.status != nullptr) w_.status->publish(st_);
    dirty_ = false;
}

}  // namespace mister::app
