// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/chd_backing.h"

#include <cstring>

#include "disc_lex.h"

namespace mister::svc {

using namespace disc::detail;

Ex<void> ChdBacking::attach(ChdHandles handles) {
    const ChdGeometry geo = handles.primary->geometry();
    if (geo.hunk_extent() == 0 || geo.hunk_bytes < geo.unit_bytes) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), geo.hunk_detail()});
    }

    if (geo.unit_bytes != proto::FileSize{kCdFrameSize}) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), geo.unit_detail()});
    }
    geo_ = geo;

    const std::size_t hunk_bytes = geo.hunk_extent();
    hunk_buf_ = std::make_unique<std::byte[]>(hunk_bytes);
    hunk_memo_ = HunkIndex::none();
    hunk_view_ = {};
    chd_ = std::move(handles.primary);

    if (handles.prefetch != nullptr) {
        if (auto a = prefetch_.get().attach(std::move(handles.prefetch), geo); !a) {
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
    geo_ = ChdGeometry{};
    hunk_memo_ = HunkIndex::none();
    hunk_view_ = {};
    prefetch_depth_ = 0;
    sync_decompress_ = 0;
}

Ex<void> ChdBacking::load_hunk(HunkIndex hunk) {
    const std::size_t hunk_bytes = geo_.hunk_extent();

    if (const std::byte* p = prefetch_.get().take(hunk); p != nullptr) {
        hunk_view_ = std::span<const std::byte>(p, hunk_bytes);
        return {};
    }
    if (hunk == hunk_memo_) {
        hunk_view_ = std::span<const std::byte>(hunk_buf_.get(), hunk_bytes);
        return {};
    }

    ++sync_decompress_;
    prefetch_.get().note_miss();
    auto r = chd_->read_hunk(hunk, std::span<std::byte>(hunk_buf_.get(), hunk_bytes));
    if (!r) {
        hunk_memo_ = HunkIndex::none();
        hunk_view_ = {};
        return std::unexpected(r.error());
    }
    hunk_memo_ = hunk;
    hunk_view_ = std::span<const std::byte>(hunk_buf_.get(), hunk_bytes);
    return {};
}

Ex<void> ChdBacking::do_read_payload(
    Lba lba,
    const Track& t,
    std::uint32_t s_off,
    std::span<std::byte> out
) {
    const std::int64_t chd_lba64 = chd_sector_pos(t, lba);
    if (chd_lba64 < 0)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), lba.v});
    const ChdUnit chd_lba{static_cast<std::uint32_t>(chd_lba64)};
    const HunkIndex hunk = geo_.hunk_of(chd_lba);
    auto h = load_hunk(hunk);
    if (!h)
        return std::unexpected(h.error());

    const std::size_t base = geo_.offset_in_hunk(chd_lba) + s_off;
    if (base + out.size() > geo_.hunk_extent()) {
        return std::unexpected(
            Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(base)}
        );
    }
    if (hunk_view_.empty()) {
        return std::unexpected(hunk.refusal(Errc::io, ERR_SITE()));
    }
    std::memcpy(out.data(), hunk_view_.data() + base, out.size());
    return {};
}

Ex<std::size_t> ChdBacking::read_subcode(Lba lba, const Track& t, std::span<std::byte> dst) {
    const std::int64_t chd_lba64 = chd_sector_pos(t, lba);
    if (chd_lba64 < 0)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), lba.v});
    const ChdUnit chd_lba{static_cast<std::uint32_t>(chd_lba64)};
    auto h = load_hunk(geo_.hunk_of(chd_lba));
    if (!h)
        return std::unexpected(h.error());

    const std::size_t base = geo_.offset_in_hunk(chd_lba) + kCdDataSize;
    if (base + kSubcodeBytes > geo_.hunk_extent()) {
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
    const std::int64_t chd_lba = chd_sector_pos(t, lba);
    if (chd_lba < 0)
        return;
    const HunkIndex hunk = geo_.hunk_of(ChdUnit{static_cast<std::uint32_t>(chd_lba)});

    if (geo_.hunk_count != HunkCount{} && !geo_.hunk_count.contains(hunk))
        return;
    prefetch_.get().submit(hunk);
}

void ChdBacking::advise_first_window(IFile& primary) noexcept {
    const std::size_t depth = (prefetch_depth_ != 0) ? prefetch_depth_ : kPrefetchMinDepth;
    (void)primary.advise(IFile::Access::WillNeed, 0, geo_.hunk_extent() * depth);
}

}
