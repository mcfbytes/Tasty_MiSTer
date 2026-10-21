// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/chd_backing.h"

#include <cstring>

#include "disc_lex.h"

namespace mister::svc {

using namespace disc::detail;

Ex<void> ChdBacking::attach(
    std::unique_ptr<IChdSource> source,
    std::unique_ptr<IChdSource> prefetch_source
) {
    const IChdSource::Geometry geo = source->geometry();
    if (geo.hunk_bytes == 0 || geo.hunk_bytes < geo.unit_bytes) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), geo.hunk_bytes});
    }

    if (geo.unit_bytes != static_cast<std::uint32_t>(kCdFrameSize)) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), geo.unit_bytes});
    }
    sectors_per_hunk_ = geo.hunk_bytes / geo.unit_bytes;
    hunk_bytes_ = geo.hunk_bytes;
    hunk_count_ = geo.hunk_count;

    hunk_buf_ = std::make_unique<std::byte[]>(hunk_bytes_);
    hunk_memo_ = kNoHunk;
    hunk_view_ = {};
    chd_ = std::move(source);

    if (prefetch_source != nullptr) {
        if (auto a = prefetch_.get().attach(
                std::move(prefetch_source),
                static_cast<std::uint32_t>(hunk_bytes_),
                hunk_count_
            );
            !a) {
            ++prefetch_refusals_;
        } else {
            prefetch_depth_ = prefetch_.get().depth();
        }
    }
    return {};
}

void ChdBacking::release() noexcept {
    chd_.reset();
    hunk_buf_.reset();
    hunk_bytes_ = 0;
    sectors_per_hunk_ = 0;
    hunk_count_ = 0;
    hunk_memo_ = kNoHunk;
    hunk_view_ = {};
    prefetch_depth_ = 0;
    sync_decompress_ = 0;
}

Ex<void> ChdBacking::load_hunk(std::uint32_t hunk) {

    if (const std::byte* p = prefetch_.get().take(hunk); p != nullptr) {
        hunk_view_ = std::span<const std::byte>(p, hunk_bytes_);
        return {};
    }
    if (hunk == hunk_memo_) {
        hunk_view_ = std::span<const std::byte>(hunk_buf_.get(), hunk_bytes_);
        return {};
    }

    ++sync_decompress_;
    prefetch_.get().note_miss();
    auto r = chd_->read_hunk(hunk, std::span<std::byte>(hunk_buf_.get(), hunk_bytes_));
    if (!r) {
        hunk_memo_ = kNoHunk;
        hunk_view_ = {};
        return std::unexpected(r.error());
    }
    hunk_memo_ = hunk;
    hunk_view_ = std::span<const std::byte>(hunk_buf_.get(), hunk_bytes_);
    return {};
}

Ex<void> ChdBacking::do_read_payload(
    Lba lba,
    const Track& t,
    std::uint32_t s_off,
    std::span<std::byte> out
) {
    const std::int64_t chd_lba64 = static_cast<std::int64_t>(lba.v) + t.offset;
    if (chd_lba64 < 0)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), lba.v});
    const std::uint32_t chd_lba = static_cast<std::uint32_t>(chd_lba64);
    const std::uint32_t hunk = chd_lba / sectors_per_hunk_;
    const std::uint32_t frame = chd_lba % sectors_per_hunk_;
    auto h = load_hunk(hunk);
    if (!h)
        return std::unexpected(h.error());

    const std::size_t base = static_cast<std::size_t>(frame) * kCdFrameSize + s_off;
    if (base + out.size() > hunk_bytes_) {
        return std::unexpected(
            Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(base)}
        );
    }
    if (hunk_view_.empty()) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), hunk});
    }
    std::memcpy(out.data(), hunk_view_.data() + base, out.size());
    return {};
}

Ex<std::size_t> ChdBacking::read_subcode(Lba lba, const Track& t, std::span<std::byte> dst) {
    const std::int64_t chd_lba64 = static_cast<std::int64_t>(lba.v) + t.offset;
    if (chd_lba64 < 0)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), lba.v});
    const std::uint32_t chd_lba = static_cast<std::uint32_t>(chd_lba64);
    auto h = load_hunk(chd_lba / sectors_per_hunk_);
    if (!h)
        return std::unexpected(h.error());

    const std::size_t base = static_cast<std::size_t>(chd_lba % sectors_per_hunk_) * kCdFrameSize
        + kCdDataSize;
    if (base + kSubcodeBytes > hunk_bytes_) {
        return std::unexpected(
            Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(base)}
        );
    }
    if (hunk_view_.empty()) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    }
    std::memcpy(dst.data(), hunk_view_.data() + base, kSubcodeBytes);
    return kSubcodeBytes;
}

void ChdBacking::prefetch_hint(Lba lba, const Track& t) {
    const std::int64_t chd_lba = static_cast<std::int64_t>(lba.v) + t.offset;
    if (chd_lba < 0)
        return;
    const std::uint32_t hunk = static_cast<std::uint32_t>(chd_lba) / sectors_per_hunk_;
    if (hunk_count_ != 0 && hunk >= hunk_count_)
        return;
    prefetch_.get().submit(hunk);
}

void ChdBacking::advise_first_window(IFile& primary) noexcept {
    const std::size_t depth = (prefetch_depth_ != 0) ? prefetch_depth_ : kPrefetchMinDepth;
    (void)
        primary.advise(IFile::Access::WillNeed, 0, static_cast<std::uint64_t>(hunk_bytes_) * depth);
}

}
