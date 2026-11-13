// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/sector_read_ahead.h"

#include <algorithm>
#include <cstring>

namespace mister::cores {

std::span<std::uint8_t> SectorReadAhead::take(
    svc::IFile& file,
    msu::TrackCursor cursor,
    std::span<std::uint8_t> dst
) noexcept {
    TASTY_SEAT_BODY(SectorReadAhead);
    if (held_ != 0 && ahead_[head_].off == cursor) {
        const Sector& s = ahead_[head_];
        std::memcpy(dst.data(), s.bytes.data(), s.got);
        head_ = (head_ + 1) % kAhead;
        --held_;
        ++tally_.ahead_hits;
        return dst.first(s.got);
    }
    const std::span<std::uint8_t> got = read_(file, cursor, dst);
    ++tally_.sync_reads;
    drop(cursor + proto::FileSize{got.size()});
    if (got.size() < msu::kSectorBytes)
        eof_ = true;
    return got;
}

void SectorReadAhead::seek(msu::TrackCursor cursor) noexcept {
    TASTY_SEAT_BODY(SectorReadAhead);
    for (std::size_t k = 0; k < held_; ++k) {
        if (ahead_[(head_ + k) % kAhead].off != cursor)
            continue;
        head_ = (head_ + k) % kAhead;
        held_ -= k;
        return;
    }
    drop(cursor);
}

bool SectorReadAhead::at_rest(proto::FileSize size) const noexcept {
    TASTY_SEAT_BODY(SectorReadAhead);
    return eof_ || held_ == kAhead || fill_at_ >= msu::TrackCursor{} + size;
}

void SectorReadAhead::refill_one(svc::IFile& file, proto::FileSize size) noexcept {
    TASTY_SEAT_BODY(SectorReadAhead);
    if (at_rest(size))
        return;
    Sector& s = ahead_[(head_ + held_) % kAhead];
    s.bytes.fill(0);
    s.off = fill_at_;
    const std::span<std::uint8_t> got = read_(file, fill_at_, s.bytes);
    s.got = static_cast<std::uint16_t>(got.size());
    ++held_;
    ++tally_.ahead_reads;
    fill_at_ += proto::FileSize{got.size()};
    if (got.size() < msu::kSectorBytes)
        eof_ = true;
}

void SectorReadAhead::drop(msu::TrackCursor from) noexcept {
    TASTY_SEAT_BODY(SectorReadAhead);
    head_ = 0;
    held_ = 0;
    fill_at_ = from;
    eof_ = false;
}

std::span<std::uint8_t> SectorReadAhead::read_(
    svc::IFile& file,
    msu::TrackCursor off,
    std::span<std::uint8_t> dst
) noexcept {
    std::size_t got = 0;
    while (got < dst.size()) {
        auto r = file.read_at(off, std::as_writable_bytes(dst.subspan(got)));
        if (!r) {
            ++tally_.read_errors;
            break;
        }
        if (*r == 0)
            break;
        got += *r;
        off += proto::FileSize{*r};
    }
    return dst.first(got);
}

}
