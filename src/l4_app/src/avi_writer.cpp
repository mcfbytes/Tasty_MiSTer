// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/avi_writer.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>

namespace mister::app {

bool AviWriter::idle() const noexcept { return w_.in == nullptr || w_.in->outbound() == 0; }

int AviWriter::park_ms() const noexcept { return -1; }

void AviWriter::serve() noexcept {
    TASTY_SEAT_BODY(AviWriter);
    if (w_.in == nullptr) return;
    for (unsigned n = 0; n < kJobsPerPass; ++n) {
        auto job = w_.in->take();
        if (!job) break;
        switch (job->kind) {
            case ChunkKind::Open:
                open_(*job);
                break;
            case ChunkKind::Data:
                data_(*job);
                break;
            case ChunkKind::Refuse:
                refuse_(*job);
                break;
            case ChunkKind::Close:
                if (job->gen == st_.gen && fd_.valid()) finalize_();
                if (job->gen == st_.gen) drop_unrated_();
                if (job->gen == st_.gen && st_.state == RecWriteState::Open)
                    st_.state = RecWriteState::Closed;
                index_.release();
                dirty_ = true;
                break;
        }
        job.complete();
    }
    if (dirty_ && w_.status != nullptr) w_.status->publish(st_);
    dirty_ = false;
}

void AviWriter::release() noexcept {
    TASTY_SEAT_BODY(AviWriter);
    while (w_.in != nullptr && w_.in->outbound() != 0)
        serve();
    if (fd_.valid()) finalize_();
    drop_unrated_();
    index_.release();
    if (w_.status != nullptr) w_.status->publish(st_);
}

void AviWriter::open_(const ChunkSlot& s) noexcept {
    if (s.gen != st_.gen) {
        if (fd_.valid()) finalize_();
        drop_unrated_();
        st_ = AviWriteStatus{};
        st_.gen = s.gen;
    } else if (st_.state == RecWriteState::Failed) {
        return;
    } else if (fd_.valid()) {
        finalize_();
        if (st_.state == RecWriteState::Failed) return;
    }
    dirty_ = true;
    st_.segment = s.segment;

    if (index_.bytes() == 0) {
        if (auto r = index_.reserve(1, kMaxEntries * avi::kIndexEntry); !r) return fail_(ENOMEM);
    }

    const int fd = ::open(s.path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0) return fail_(errno);
    fd_.reset(fd);
    entries_ = 0;
    f_ = avi::Fields{};
    f_.width = s.width;
    f_.height = s.height;
    f_.codec = s.codec;
    f_.rate = avi::kTickHz;
    frame_mul_ = s.frame_mul == 0 ? 1 : s.frame_mul;
    f_.scale = scaled_vtime_(avi::kDefaultVtime);
    rate_known_ = false;
    if (avi::plausible_vtime(s.vtime)) learn_rate_(s.vtime);
    avi::Header h{};
    avi::header(f_, h);
    if (!write_all_(h.data(), h.size())) return;
    pos_ = avi::kHeaderBytes;
    st_.state = RecWriteState::Open;
}

void AviWriter::refuse_(const ChunkSlot& s) noexcept {
    if (s.gen != st_.gen) {
        if (fd_.valid()) finalize_();
        drop_unrated_();
        st_ = AviWriteStatus{};
        st_.gen = s.gen;
    }
    fail_(ENOMEM);
}

void AviWriter::data_(const ChunkSlot& s) noexcept {
    if (s.gen != st_.gen || st_.state != RecWriteState::Open || !fd_.valid()) {
        st_.dropped += s.chunks;
        dirty_ = true;
        return;
    }
    if (!write_all_(s.bytes, s.used)) {
        st_.dropped += s.chunks;
        return;
    }
    std::byte* const idx = index_.stripe(0).data();
    std::uint64_t at = pos_;
    for (std::uint32_t i = 0; i < s.chunks; ++i) {
        const std::uint32_t len = s.desc[i] & ~ChunkSlot::kKey;
        if (entries_ < kMaxEntries) {
            avi::index_entry((s.desc[i] & ChunkSlot::kKey) != 0,
                             static_cast<std::uint32_t>(at - avi::kMoviFourcc), len,
                             std::span<std::byte, avi::kIndexEntry>(
                                 idx + std::size_t{entries_} * avi::kIndexEntry, avi::kIndexEntry));
            ++entries_;
        }
        f_.max_chunk = std::max(f_.max_chunk, len);
        at += avi::chunk_bytes(len);
    }

    if (!rate_known_ && avi::plausible_vtime(s.vtime)) learn_rate_(s.vtime);
    pos_ += s.used;
    f_.frames += s.chunks;
    f_.movi_bytes += s.used;
    st_.frames += s.chunks;
    st_.bytes += s.used;
    dirty_ = true;
}

void AviWriter::finalize_() noexcept {
    std::array<std::byte, avi::kChunkHead> head{};
    avi::index_head(entries_, head);
    const std::size_t body = std::size_t{entries_} * avi::kIndexEntry;
    if (write_all_(head.data(), head.size()) && write_all_(index_.stripe(0).data(), body)) {
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
    entries_ = 0;
    dirty_ = true;
}

static_assert(std::uint64_t{avi::kVtimeMax} * kRecEveryMax <= 0xFFFF'FFFFu);

std::uint32_t AviWriter::scaled_vtime_(std::uint32_t vtime) const noexcept {
    const std::uint32_t mul = frame_mul_ == 0 ? 1u : frame_mul_;
    const std::uint64_t product = std::uint64_t{vtime} * mul;
    return static_cast<std::uint32_t>(product > 0xFFFF'FFFFu ? 0xFFFF'FFFFu : product);
}

void AviWriter::learn_rate_(std::uint32_t vtime) noexcept {
    f_.scale = scaled_vtime_(vtime);
    rate_known_ = true;
    for (std::size_t i = 0; i < nunrated_; ++i) {
        Unrated& u = unrated_[i];
        u.f.scale = f_.scale;
        avi::Header h{};
        avi::header(u.f, h);
        std::size_t off = 0;
        while (off < h.size()) {
            const ssize_t w =
                ::pwrite(u.fd.get(), h.data() + off, h.size() - off, static_cast<off_t>(off));
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) break;
            off += static_cast<std::size_t>(w);
        }
        if (off == h.size()) ++st_.header_rewrites;
    }
    drop_unrated_();
}

void AviWriter::drop_unrated_() noexcept {
    for (std::size_t i = 0; i < nunrated_; ++i)
        unrated_[i] = Unrated{};
    if (nunrated_ != 0) dirty_ = true;
    nunrated_ = 0;
}

void AviWriter::write_header_() noexcept {
    avi::Header h{};
    avi::header(f_, h);
    std::size_t off = 0;
    while (off < h.size() && fd_.valid()) {
        const ssize_t w =
            ::pwrite(fd_.get(), h.data() + off, h.size() - off, static_cast<off_t>(off));
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return fail_(w < 0 ? errno : EIO);
        off += static_cast<std::size_t>(w);
    }
    ++st_.header_rewrites;
    dirty_ = true;
}

void AviWriter::sync_() noexcept {
    if (::fdatasync(fd_.get()) != 0 && errno != EINVAL) return fail_(errno);
    ++st_.syncs;
    dirty_ = true;
}

bool AviWriter::write_all_(const std::byte* p, std::size_t n) noexcept {
    std::size_t off = 0;
    while (off < n && fd_.valid()) {
        const ssize_t w = ::write(fd_.get(), p + off, n - off);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) {
            fail_(w < 0 ? errno : EIO);
            return false;
        }
        off += static_cast<std::size_t>(w);
    }
    return off == n;
}

void AviWriter::fail_(int err) noexcept {
    if (st_.err == 0) st_.err = err;
    st_.state = RecWriteState::Failed;
    fd_.reset();
    entries_ = 0;
    dirty_ = true;
}

}  // namespace mister::app
