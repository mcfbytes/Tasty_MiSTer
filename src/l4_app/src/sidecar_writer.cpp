// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/sidecar_writer.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace mister::app {

bool SidecarWriter::idle() const noexcept { return w_.in == nullptr || w_.in->size() == 0; }

int SidecarWriter::park_ms() const noexcept { return -1; }

void SidecarWriter::serve() noexcept {
    TASTY_SEAT_BODY(SidecarWriter);
    if (w_.in == nullptr) return;
    for (unsigned n = 0; n < kMsgsPerPass; ++n) {
        const auto m = w_.in->pop();
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
    if (dirty_ && w_.status != nullptr) w_.status->publish(st_);
    dirty_ = false;
}

void SidecarWriter::release() noexcept {
    TASTY_SEAT_BODY(SidecarWriter);
    if (fd_.valid()) close_();
    if (w_.status != nullptr) w_.status->publish(st_);
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
    std::memcpy(buf_, kHeader, n);
    used_ = n;
}

bool SidecarWriter::grow_(std::size_t need) noexcept {
    std::size_t cap = cap_ == 0 ? kBufBytes : cap_;
    while (cap < used_ + need)
        cap *= 2;
    if (cap == cap_) return true;
    void* const p = buf_ == nullptr ? ::mmap(nullptr, cap, PROT_READ | PROT_WRITE,
                                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
                                    : ::mremap(buf_, cap_, cap, MREMAP_MAYMOVE);
    if (p == MAP_FAILED) return false;
    buf_ = static_cast<char*>(p);
    cap_ = cap;
    return true;
}

void SidecarWriter::unmap_() noexcept {
    if (buf_ != nullptr) (void)::munmap(buf_, cap_);
    buf_ = nullptr;
    cap_ = 0;
}

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
        if (st_.state == RecWriteState::Open && n > cap_) fail_(ENOMEM);
    }
    if (n == 0 || st_.state != RecWriteState::Open) {
        ++st_.dropped;
        dirty_ = true;
        return;
    }
    std::memcpy(buf_ + used_, line, n);
    used_ += n;
    ++st_.rows;
    dirty_ = true;
}

void SidecarWriter::flush_() noexcept {
    std::size_t off = 0;
    while (off < used_ && fd_.valid()) {
        const ssize_t w = ::write(fd_.get(), buf_ + off, used_ - off);
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
