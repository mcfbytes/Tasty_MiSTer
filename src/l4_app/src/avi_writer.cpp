// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/avi_writer.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>

#include "app/chunk_dispatch.h"
#include "app/log_clock.h"
#include "infra/errno.h"
#include "os/write_all.h"

namespace mister::app {

bool AviWriter::idle() const noexcept {
    return w_.in.outbound() == 0;
}

infra::ParkTimeout AviWriter::park_ms() const noexcept {
    return infra::kForever;
}

void AviWriter::serve() noexcept {
    TASTY_SEAT_BODY(AviWriter);
    for (unsigned n = 0; n < kJobsPerPass; ++n) {
        auto job = w_.in.take();
        if (!job)
            break;
        infra::dispatch<ChunkRoutes>(*job, *this);
        job.complete();
    }
    if (dirty_)
        w_.status.publish(st_);
    dirty_ = false;
}

void AviWriter::release() noexcept {
    TASTY_SEAT_BODY(AviWriter);
    while (w_.in.outbound() != 0)
        serve();
    if (fd_.valid())
        finalize_();
    drop_unrated_();
    index_.release();
    w_.status.publish(st_);
}

void AviWriter::on(const ChunkSlot::Open& s, ControlGeneration gen) noexcept {
    if (gen != st_.gen) {
        adopt_(gen);
    } else if (st_.state == RecWriteState::Failed) {
        return;
    } else if (fd_.valid()) {
        finalize_();
        if (st_.state == RecWriteState::Failed)
            return;
    }
    dirty_ = true;
    st_.segment = s.segment;

    if (index_.bytes() == 0) {
        if (auto r = index_.reserve(1, kMaxEntries.scaled_by(avi::kIndexEntry)); !r)
            return fail_(infra::Errno{ENOMEM});
    }

    const int fd = ::open(s.path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0)
        return fail_(infra::Errno{errno});
    fd_.reset(fd);
    entries_ = AviFrameCount{};
    f_ = avi::Fields{};
    f_.width = s.size.width;
    f_.rows = s.size.rows;
    f_.codec = s.codec;
    f_.rate = FrameTicks::kTickHz;
    frame_mul_ = s.frame_mul;
    f_.scale = FrameTicks::k60Hz().dw_scale(frame_mul_);
    rate_known_ = false;
    if (s.rate.known())
        learn_rate_(s.rate);
    const auto h = avi::header(f_);
    if (!write_all_(h))
        return;
    pos_ = avi::kHeaderBytes;
    st_.state = RecWriteState::Open;
}

void AviWriter::on(const ChunkSlot::Refuse&, ControlGeneration gen) noexcept {
    if (gen != st_.gen)
        adopt_(gen);
    fail_(infra::Errno{ENOMEM});
}

void AviWriter::on(const ChunkSlot::Data& s, ControlGeneration gen) noexcept {
    if (gen != st_.gen || st_.state != RecWriteState::Open || !fd_.valid()) {
        st_.dropped += s.chunks;
        dirty_ = true;
        return;
    }
    if (!write_all_(s.bytes.first(s.used))) {
        st_.dropped += s.chunks;
        return;
    }
    std::byte* const idx = index_.stripe(0).data();
    std::uint64_t at = pos_;
    for (std::uint32_t i = 0; i < s.chunks; ++i) {
        const ChunkDesc desc = ChunkSlot::desc_word(s.desc, i);
        const std::uint32_t len = desc.payload();
        if (entries_ < kMaxEntries) {
            avi::index_entry(
                desc.key(),
                static_cast<std::uint32_t>(at - avi::kMoviFourcc),
                len,
                std::span<std::byte, avi::kIndexEntry>(
                    idx + entries_.scaled_by(avi::kIndexEntry),
                    avi::kIndexEntry
                )
            );
            entries_ += AviFrameCount{1};
        }
        f_.max_chunk = std::max(f_.max_chunk, len);
        at += avi::chunk_bytes(len);
    }

    if (!rate_known_ && s.rate.known())
        learn_rate_(s.rate);
    pos_ += s.used;
    f_.frames += s.chunks;
    f_.movi_bytes += s.used;
    st_.frames += s.chunks;
    st_.bytes += s.used;
    dirty_ = true;
}

void AviWriter::on(const ChunkSlot::Close&, ControlGeneration gen) noexcept {
    if (gen == st_.gen && fd_.valid())
        finalize_();
    if (gen == st_.gen)
        drop_unrated_();
    if (gen == st_.gen && st_.state == RecWriteState::Open)
        st_.state = RecWriteState::Closed;
    if (gen == st_.gen)
        index_.release();
    dirty_ = true;
}

void AviWriter::misrouted(const ChunkSlot&) noexcept {
    TASTY_SEAT_BODY(AviWriter);
}

void AviWriter::adopt_(ControlGeneration gen) noexcept {
    if (fd_.valid())
        finalize_();
    drop_unrated_();
    st_ = AviWriteStatus{};
    st_.gen = gen;
}

void AviWriter::finalize_() noexcept {
    std::array<std::byte, avi::kChunkHead> head{};
    avi::index_head(entries_, head);
    const std::size_t body = entries_.scaled_by(avi::kIndexEntry);
    if (write_all_(head) && write_all_(index_.stripe(0).first(body))) {
        f_.index_bytes = avi::kChunkHead + body;
        write_header_();
        if (fd_.valid()) {
            sync_();
            ++st_.segments;
        }
    }
    if (!rate_known_ && fd_.valid()) {
        if (nunrated_ == kUnrated) {
            std::rotate(unrated_.begin(), unrated_.begin() + 1, unrated_.end());
            --nunrated_;
        }
        unrated_[nunrated_++] = Unrated{.fd = std::move(fd_), .f = f_};
    }
    fd_.reset();
    entries_ = AviFrameCount{};
    dirty_ = true;
}

void AviWriter::learn_rate_(FrameTicks rate) noexcept {
    f_.scale = rate.dw_scale(frame_mul_);
    rate_known_ = true;
    for (std::size_t i = 0; i < nunrated_; ++i) {
        Unrated& u = unrated_[i];
        u.f.scale = f_.scale;
        const auto h = avi::header(u.f);
        if (const auto wrote = os::rewrite_head(u.fd, h); wrote) {
            ++st_.header_rewrites;
        } else if (!st_.err.is_set()) {

            st_.err = wrote.error().os_errno();
            dirty_ = true;
        }
    }
    drop_unrated_();
}

void AviWriter::drop_unrated_() noexcept {
    for (std::size_t i = 0; i < nunrated_; ++i)
        unrated_[i] = Unrated{};
    if (nunrated_ != 0)
        dirty_ = true;
    nunrated_ = 0;
}

void AviWriter::write_header_() noexcept {
    const auto h = avi::header(f_);
    if (const auto wrote = os::rewrite_head(fd_, h); !wrote)
        return fail_(wrote.error().os_errno());
    ++st_.header_rewrites;
    dirty_ = true;
}

void AviWriter::sync_() noexcept {
    if (::fdatasync(fd_.get()) != 0 && errno != EINVAL)
        return fail_(infra::Errno{errno});
    ++st_.syncs;
    dirty_ = true;
}

bool AviWriter::write_all_(std::span<const std::byte> bytes) noexcept {
    if (const auto wrote = os::write_all(fd_, bytes); !wrote) {
        fail_(wrote.error().os_errno());
        return false;
    }
    return true;
}

void AviWriter::fail_(infra::Errno err) noexcept {
    if (!st_.err.is_set())
        st_.err = err;
    st_.state = RecWriteState::Failed;
    fd_.reset();
    entries_ = AviFrameCount{};
    dirty_ = true;
}

}
