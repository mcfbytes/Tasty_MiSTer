// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/sidecar_writer.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace mister::app {

bool SidecarWriter::idle() const noexcept { return w_.in.size() == 0; }

int SidecarWriter::park_ms() const noexcept { return -1; }

void SidecarWriter::serve() noexcept {
    TASTY_SEAT_BODY(SidecarWriter);
    for (unsigned n = 0; n < kMsgsPerPass; ++n) {
        const auto m = w_.in.pop();
        if (!m) break;
        switch (m->kind) {
            case SidecarKind::Open:
                open_(*m);
                break;
            case SidecarKind::Row:
                row_(*m);
                break;
            case SidecarKind::Close:
                if (m->gen == st_.gen) close_();
                break;
        }
    }
    if (dirty_) w_.status.publish(st_);
    dirty_ = false;
}

void SidecarWriter::release() noexcept {
    TASTY_SEAT_BODY(SidecarWriter);
    if (fd_.valid()) close_();
    w_.status.publish(st_);
}

void SidecarWriter::open_(const SidecarMsg& m) noexcept {
    if (fd_.valid()) close_();
    st_ = RecWriteStatus{};
    st_.gen = m.gen;
    dirty_ = true;

    const int fd = ::open(m.path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0) return fail_(errno);
    fd_.reset(fd);
    st_.state = RecWriteState::Open;
    const std::size_t n = std::strlen(kHeader);
    used_ = 0;
    if (!grow_(n)) return fail_(ENOMEM);
    std::memcpy(buf_.data(), kHeader, n);
    used_ = n;
}

bool SidecarWriter::grow_(std::size_t need) noexcept {
    const std::size_t had = buf_.size();
    std::size_t cap = had == 0 ? kBufBytes : had;
    while (cap < used_ + need)
        cap *= 2;
    if (cap == had) return true;
    return buf_.resize(cap);
}

void SidecarWriter::unmap_() noexcept { buf_.release(); }

void SidecarWriter::row_(const SidecarMsg& m) noexcept {
    if (m.gen != st_.gen || st_.state != RecWriteState::Open) {
        ++st_.dropped;
        dirty_ = true;
        return;
    }
    char line[160];
    const int len = std::snprintf(line, sizeof line,
                                  "%" PRIu64 "\t%u\t%" PRId64 "\t%s\t%" PRId32 "\t%08" PRIx32
                                  "\t%u\t%u\t%" PRId32 "\t%" PRId32 "\n",
                                  m.stamp.core_frame, static_cast<unsigned>(m.stamp.header_ctr),
                                  m.stamp.capture_ns, dup_reason_name(m.stamp.dup),
                                  m.stamp.movie_frame, m.hash, static_cast<unsigned>(m.width),
                                  static_cast<unsigned>(m.height), m.segment, m.seg_frame);
    const auto n = static_cast<std::size_t>(len > 0 ? len : 0);
    if (n != 0 && used_ + n > kSpillBytes) flush_();
    if (n != 0 && st_.state == RecWriteState::Open && !grow_(n)) {

        flush_();
        if (st_.state == RecWriteState::Open && n > buf_.size()) fail_(ENOMEM);
    }
    if (n == 0 || st_.state != RecWriteState::Open) {
        ++st_.dropped;
        dirty_ = true;
        return;
    }
    std::memcpy(buf_.data() + used_, line, n);
    used_ += n;
    ++st_.rows;
    dirty_ = true;
}

void SidecarWriter::flush_() noexcept {
    std::size_t off = 0;
    while (off < used_ && fd_.valid()) {
        const ssize_t w = ::write(fd_.get(), buf_.data() + off, used_ - off);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) {
            used_ = 0;
            return fail_(w < 0 ? errno : EIO);
        }
        off += static_cast<std::size_t>(w);
        st_.bytes += static_cast<std::uint64_t>(w);
    }
    used_ = 0;
    dirty_ = true;
}

void SidecarWriter::sync_() noexcept {
    if (::fdatasync(fd_.get()) != 0 && errno != EINVAL) return fail_(errno);
    ++st_.syncs;
    dirty_ = true;
}

void SidecarWriter::close_() noexcept {
    flush_();
    if (fd_.valid()) {
        sync_();
        if (st_.state == RecWriteState::Open) st_.state = RecWriteState::Closed;
    }
    fd_.reset();
    unmap_();
    dirty_ = true;
}

void SidecarWriter::fail_(int err) noexcept {
    if (st_.err == 0) st_.err = err;
    st_.state = RecWriteState::Failed;
    fd_.reset();
    used_ = 0;
    unmap_();
    dirty_ = true;
}

}  // namespace mister::app
