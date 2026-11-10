// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/chd_backing.h"

#include <cstdint>
#include <cstring>
#include <optional>

#include "disc_lex.h"

namespace mister::svc {

using namespace disc::detail;

namespace {

std::optional<ChdUnit> chd_sector_pos(const Track& t, Lba lba) noexcept {
    const std::int64_t pos = static_cast<std::int64_t>(lba.v) + t.offset;
    if (pos < 0)
        return std::nullopt;
    return ChdUnit{static_cast<std::uint32_t>(pos)};
}

}

Ex<void> ChdBacking::attach(ChdHandles handles) {
    const ChdGeometry geo = handles.primary->geometry();
    if (geo.hunk_extent() == 0 || geo.hunk_bytes < geo.unit_bytes) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), geo.hunk_detail()});
    }

    if (geo.unit_bytes != proto::FileSize{kFullFrameBytes}) {
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
    prefetch_depth_ = HunkCount{};
    sync_decompress_ = 0;
}

Ex<void> ChdBacking::load_hunk(HunkIndex hunk) {
    const std::size_t hunk_bytes = geo_.hunk_extent();

    if (const std::span<const std::byte> resident = prefetch_.get().take(hunk); !resident.empty()) {
        hunk_view_ = resident;
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
    const std::optional<ChdUnit> chd_lba = chd_sector_pos(t, lba);
    if (!chd_lba)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), lba.v});
    const HunkIndex hunk = geo_.hunk_of(*chd_lba);
    auto h = load_hunk(hunk);
    if (!h)
        return std::unexpected(h.error());

    const std::size_t base = geo_.offset_in_hunk(*chd_lba) + s_off;
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

Ex<std::span<std::byte>> ChdBacking::read_subcode(
    Lba lba,
    const Track& t,
    std::span<std::byte> dst
) {
    const std::optional<ChdUnit> chd_lba = chd_sector_pos(t, lba);
    if (!chd_lba)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), lba.v});
    auto h = load_hunk(geo_.hunk_of(*chd_lba));
    if (!h)
        return std::unexpected(h.error());

    const std::size_t base = geo_.offset_in_hunk(*chd_lba) + kRawFrameBytes;
    if (base + kSubcodeBytes > geo_.hunk_extent()) {
        return std::unexpected(
            Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(base)}
        );
    }
    if (hunk_view_.empty()) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    }
    std::memcpy(dst.data(), hunk_view_.data() + base, kSubcodeBytes);
    return dst.first(kSubcodeBytes);
}

void ChdBacking::advise_first_window(IFile& primary) noexcept {
    const HunkCount depth = prefetch_depth_ != HunkCount{} ? prefetch_depth_ : kPrefetchMinHunks;
    (void)primary.advise(IFile::Access::WillNeed, 0, geo_.extent_of(depth));
}

}
