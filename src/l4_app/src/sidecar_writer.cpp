// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/sidecar_writer.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "infra/errno.h"
#include "infra/message_sum.h"
#include "os/write_all.h"

namespace mister::app {

void SidecarWriter::on(const SidecarMsg& m) noexcept {
    TASTY_SEAT_BODY(SidecarWriter);
    infra::dispatch<infra::AllRouted>(m, *this);
}

void SidecarWriter::misrouted(const SidecarMsg&) noexcept {
    TASTY_SEAT_BODY(SidecarWriter);
    ++st_.dropped;
    dirty_ = true;
}

void SidecarWriter::on(const SidecarMsg::Open& o, const SidecarMsg::Head& head) noexcept {
    TASTY_SEAT_BODY(SidecarWriter);
    if (fd_.valid())
        close_();
    st_ = RecWriteStatus{};
    st_.gen = head.gen;
    dirty_ = true;

    const int fd = ::open(o.path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0)
        return fail_(infra::Errno{errno});
    fd_.reset(fd);
    st_.state = RecWriteState::Open;
    const std::size_t n = std::strlen(kHeader);
    used_ = 0;
    if (!grow_(n))
        return fail_(infra::Errno{ENOMEM});
    std::memcpy(buf_.data(), kHeader, n);
    used_ = n;
}

void SidecarWriter::publish() noexcept {
    if (dirty_)
        w_.status.publish(st_);
    dirty_ = false;
}

void SidecarWriter::release() noexcept {
    TASTY_SEAT_BODY(SidecarWriter);
    if (fd_.valid())
        close_();
    w_.status.publish(st_);
}

bool SidecarWriter::grow_(std::size_t need) noexcept {
    const std::size_t had = buf_.size();
    std::size_t cap = had == 0 ? kBufBytes : had;
    while (cap < used_ + need)
        cap *= 2;
    if (cap == had)
        return true;
    return buf_.resize(cap).has_value();
}

void SidecarWriter::unmap_() noexcept {
    buf_.release();
}

void SidecarWriter::on(const SidecarMsg::Row& row, const SidecarMsg::Head& head) noexcept {
    TASTY_SEAT_BODY(SidecarWriter);
    if (head.gen != st_.gen || st_.state != RecWriteState::Open) {
        ++st_.dropped;
        dirty_ = true;
        return;
    }
    char line[160];

    const auto be = row.hash.to_be_bytes();
    char hash_hex[9]{};
    constexpr char kHexDigits[] = "0123456789abcdef";
    constexpr unsigned kNibbleBits = 4;
    constexpr std::uint8_t kLowNibble = 0x0F;
    std::size_t at = 0;
    for (const std::uint8_t byte : be) {
        hash_hex[at++] = kHexDigits[byte >> kNibbleBits];
        hash_hex[at++] = kHexDigits[byte & kLowNibble];
    }
    const int len = std::snprintf(
        line,
        sizeof line,
        "%" PRIu64 "\t%s\t%" PRId64 "\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n",
        row.stamp.core_frame.number(),
        row.stamp.header_ctr.text().c_str(),
        row.stamp.capture_ns,
        dup_reason_name(row.stamp.dup),
        row.stamp.movie_frame.text().c_str(),
        hash_hex,
        row.width.text().c_str(),
        row.height.text().c_str(),
        row.place.segment_column().c_str(),
        row.place.frame_column().c_str()
    );
    const auto n = static_cast<std::size_t>(len > 0 ? len : 0);
    if (n != 0 && used_ + n > kSpillBytes)
        flush_();
    if (n != 0 && st_.state == RecWriteState::Open && !grow_(n)) {

        flush_();
        if (st_.state == RecWriteState::Open && n > buf_.size())
            fail_(infra::Errno{ENOMEM});
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

void SidecarWriter::on(const SidecarMsg::Close&, const SidecarMsg::Head& head) noexcept {
    TASTY_SEAT_BODY(SidecarWriter);
    if (head.gen == st_.gen)
        close_();
}

void SidecarWriter::flush_() noexcept {
    const std::size_t bytes = used_;
    used_ = 0;
    if (const auto wrote = os::write_all(fd_, buf_.view().first(bytes)); !wrote)
        return fail_(wrote.error().os_errno());
    st_.bytes += bytes;
    dirty_ = true;
}

void SidecarWriter::sync_() noexcept {
    if (::fdatasync(fd_.get()) != 0 && errno != EINVAL)
        return fail_(infra::Errno{errno});
    ++st_.syncs;
    dirty_ = true;
}

void SidecarWriter::close_() noexcept {
    flush_();
    if (fd_.valid()) {
        sync_();
        if (st_.state == RecWriteState::Open)
            st_.state = RecWriteState::Closed;
    }
    fd_.reset();
    unmap_();
    dirty_ = true;
}

void SidecarWriter::fail_(infra::Errno err) noexcept {
    if (!st_.err.is_set())
        st_.err = err;
    st_.state = RecWriteState::Failed;
    fd_.reset();
    used_ = 0;
    unmap_();
    dirty_ = true;
}

}
