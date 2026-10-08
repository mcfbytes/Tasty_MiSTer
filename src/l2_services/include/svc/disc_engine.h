// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "infra/error.h"
#include "proto/types.h"
#include "svc/chd_source.h"
#include "svc/cue_policy.h"
#include "svc/disc_counters.h"
#include "svc/disc_geometry.h"
#include "svc/frame_shaper.h"
#include "svc/image_opener.h"
#include "svc/toc.h"
#include "svc/track.h"
#include "svc/types.h"
#include "svc/chd_prefetch.h"
#include "svc/vfs.h"
#include "infra/seat.h"

namespace mister::svc {

using proto::Lba;

struct ChdMetaRow {
    std::string_view text;
    bool v2 = true;
};

class DiscEngine {
    TASTY_SEAT_RESIDENT(Io);

public:
    static Ex<DiscEngine> create(const Vfs& vfs, CuePolicy policy, ChdPrefetch& prefetch);

    static Ex<DiscEngine> create(const IImageOpener& opener, CuePolicy policy,
                                 ChdPrefetch& prefetch);

    Ex<void> mount(std::string_view image_path);
    Ex<void> unmount();

    [[nodiscard]] Ex<void> mount_chd(std::unique_ptr<IChdSource> source,
                                     std::unique_ptr<IChdSource> prefetch_source = nullptr);

    Ex<std::size_t> read_sector(Lba lba, std::span<std::byte> dst);

    Ex<std::size_t> read_subcode(Lba lba, std::span<std::byte> dst);

    Ex<std::size_t> read_user_data(Lba lba, std::span<std::byte> dst);
    Ex<std::size_t> read_raw_frame(Lba lba, std::span<std::byte> dst);

    [[nodiscard]] Ex<std::size_t> read_full_frame(Lba lba, std::span<std::byte> dst);

    static constexpr CddaOrder kWireCddaOrder = CddaOrder::LittleEndian;
    CddaOrder cdda_order() const noexcept { return cdda_order_; }

    const Toc& toc() const noexcept { return toc_; }
    bool mounted() const noexcept { return mounted_; }

    std::optional<TrackIndex> track_for_lba(Lba lba) const;

    std::uint32_t seek_ms(Lba from_lba, Lba to_lba) const;

    void prefetch_hint(Lba lba);

    static constexpr std::uint32_t kAdviseAheadSectors = 75;

    void advise_ahead(Lba lba) noexcept;

    std::uint32_t chd_hunk_bytes() const noexcept {
        return chd_ != nullptr ? static_cast<std::uint32_t>(hunk_bytes_) : 0;
    }

    [[nodiscard]] DiscGeometry geometry() const noexcept {
        return DiscGeometry{.toc = toc_, .hunk_bytes = chd_hunk_bytes(), .mounted = mounted_};
    }

    [[nodiscard]] DiscCounters counters() const noexcept {
        return DiscCounters{.sync_decompress = sync_decompress_,
                            .park_timeouts = park_timeouts_,
                            .prefetch_refusals = prefetch_refusals_};
    }

    std::uint32_t sync_decompress() const noexcept { return sync_decompress_; }
    std::uint32_t park_timeouts() const noexcept { return park_timeouts_; }
    std::uint32_t prefetch_refusals() const noexcept { return prefetch_refusals_; }

private:
    explicit DiscEngine(CuePolicy p, ChdPrefetch& prefetch);

    enum class Form : std::uint8_t { Native, UserData, RedBook };

    Ex<std::unique_ptr<IFile>> open_file(std::string_view path) const;

    [[nodiscard]] Ex<std::string> read_cue_text(std::string_view cue_path) const;
    [[nodiscard]] Ex<void> mount_cue(std::string_view cue_path);

    [[nodiscard]] Ex<void> mount_cue_index_triple(std::string_view cue_path);

    [[nodiscard]] Ex<void> mount_cue_index_pair(std::string_view cue_path);

    void open_sub_or_cdg_(std::string_view cue_path);

    [[nodiscard]] Ex<std::size_t> read_cdg_subcode_(std::uint32_t at, std::span<std::byte> dst);
    [[nodiscard]] Ex<void> mount_iso(std::string_view iso_path);

    [[nodiscard]] Ex<void> declare_iso_2048(std::unique_ptr<IFile> f, FileSize sz);

    Ex<void> mount_chd_path(std::string_view chd_path);
    Ex<std::size_t> read_form(Lba lba, std::span<std::byte> dst, Form form);

    void normalize_cdda_(const Track& t, Form form, std::span<std::byte> frame) const noexcept;

    std::optional<TrackIndex> source_track(Lba lba) const;

    [[nodiscard]] Ex<void> load_hunk(std::uint32_t hunk);

    CuePolicy policy_;
    Toc toc_{};
    bool mounted_ = false;

    CddaOrder cdda_order_ = kWireCddaOrder;

    const Vfs* vfs_ = nullptr;
    const IImageOpener* opener_ = nullptr;

    std::unique_ptr<IFile> files_[kMaxTracks]{};
    std::uint16_t file_count_ = 0;
    std::unique_ptr<IFile> sub_;

    std::uint32_t sub_origin_ = 0;
    bool sub_cdg_ = false;

    std::unique_ptr<IFrameShaper> frame_shaper_;
    std::unique_ptr<IFrameShaper> sub_shaper_;

    std::unique_ptr<IChdSource> chd_;
    std::unique_ptr<std::byte[]> hunk_buf_;
    std::size_t hunk_bytes_ = 0;
    std::uint32_t sectors_per_hunk_ = 0;
    std::uint32_t hunk_count_ = 0;
    static constexpr std::uint32_t kNoHunk = 0xFFFFFFFFu;
    std::uint32_t hunk_memo_ = kNoHunk;

    const std::byte* hunk_ptr_ = nullptr;

    std::reference_wrapper<ChdPrefetch> prefetch_;

    std::size_t prefetch_depth_ = 0;
    std::uint32_t sync_decompress_ = 0;

    std::uint32_t park_timeouts_ = 0;
    std::uint32_t prefetch_refusals_ = 0;
};

}  // namespace mister::svc
