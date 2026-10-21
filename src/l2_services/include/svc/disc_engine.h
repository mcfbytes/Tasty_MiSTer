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
#include "infra/opt_ref.h"
#include "proto/types.h"
#include "svc/chd_backing.h"
#include "svc/chd_meta_row.h"
#include "svc/chd_source.h"
#include "svc/cue_policy.h"
#include "svc/cue_program.h"
#include "svc/disc_counters.h"
#include "svc/disc_file_access.h"
#include "svc/file_table_backing.h"
#include "svc/disc_geometry.h"
#include "svc/frame_shaper.h"
#include "svc/image_opener.h"
#include "svc/subcode_sidecar.h"
#include "svc/toc.h"
#include "svc/track.h"
#include "svc/types.h"
#include "svc/chd_prefetch.h"
#include "svc/vfs.h"
#include "infra/seat.h"

namespace mister::svc {

using proto::Lba;

class DiscEngine final {
    TASTY_SEAT_RESIDENT(Io);

public:
    [[nodiscard]] static Ex<DiscEngine> create(
        const Vfs& vfs,
        CuePolicy policy,
        ChdPrefetch& prefetch
    );

    [[nodiscard]] static Ex<DiscEngine> create(
        const IImageOpener& opener,
        CuePolicy policy,
        ChdPrefetch& prefetch
    );

    [[nodiscard]] Ex<void> mount(std::string_view image_path);
    [[nodiscard]] Ex<void> unmount();

    [[nodiscard]] Ex<void> mount_chd(
        std::unique_ptr<IChdSource> source,
        std::unique_ptr<IChdSource> prefetch_source = nullptr
    );

    [[nodiscard]] Ex<std::size_t> read_sector(Lba lba, std::span<std::byte> dst);

    [[nodiscard]] Ex<std::size_t> read_subcode(Lba lba, std::span<std::byte> dst);

    [[nodiscard]] Ex<std::size_t> read_user_data(Lba lba, std::span<std::byte> dst);
    [[nodiscard]] Ex<std::size_t> read_raw_frame(Lba lba, std::span<std::byte> dst);

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

    std::uint32_t chd_hunk_bytes() const noexcept { return chd_.hunk_bytes(); }

    [[nodiscard]] DiscGeometry geometry() const noexcept {
        return DiscGeometry{.toc = toc_, .hunk_bytes = chd_hunk_bytes(), .mounted = mounted_};
    }

    [[nodiscard]] DiscCounters counters() const noexcept { return chd_.counters(); }

    std::uint32_t sync_decompress() const noexcept { return chd_.sync_decompress(); }

    std::uint32_t park_timeouts() const noexcept { return chd_.park_timeouts(); }

    std::uint32_t prefetch_refusals() const noexcept { return chd_.prefetch_refusals(); }

private:
    DiscEngine(
        CuePolicy p,
        ChdPrefetch& prefetch,
        infra::OptRef<const Vfs> vfs,
        infra::OptRef<const IImageOpener> opener
    );

    enum class Form : std::uint8_t { Native, UserData, RedBook };

    [[nodiscard]] Ex<void> mount_cue(std::string_view cue_path);

    void open_sub_or_cdg_(std::string_view cue_path);
    [[nodiscard]] Ex<void> mount_iso(std::string_view iso_path);

    [[nodiscard]] Ex<void> declare_iso_2048(std::unique_ptr<IFile> f, FileSize sz);

    [[nodiscard]] Ex<void> mount_chd_path(std::string_view chd_path);
    [[nodiscard]] Ex<std::size_t> read_form(Lba lba, std::span<std::byte> dst, Form form);

    void normalize_cdda_(const Track& t, Form form, std::span<std::byte> frame) const noexcept;

    std::optional<TrackIndex> source_track(Lba lba) const;

    CuePolicy policy_;

    std::unique_ptr<ICueProgram> cue_;
    Toc toc_{};
    bool mounted_ = false;

    CddaOrder cdda_order_ = kWireCddaOrder;

    DiscFileAccess files_io_;

    FileTableBacking table_;
    SubcodeSidecar subcode_;

    std::unique_ptr<IFrameShaper> frame_shaper_;
    std::unique_ptr<IFrameShaper> sub_shaper_;

    ChdBacking chd_;
};

}
