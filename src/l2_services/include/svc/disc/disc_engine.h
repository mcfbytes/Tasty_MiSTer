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
#include "infra/prelude.h"
#include "proto/types.h"
#include "svc/disc/chd_backing.h"
#include "svc/disc/chd_meta_row.h"
#include "svc/disc/chd_source.h"
#include "svc/disc/chd_track.h"
#include "svc/disc/cue_policy.h"
#include "svc/disc/cue_program.h"
#include "svc/disc/disc_counters.h"
#include "svc/disc_file_access.h"
#include "svc/disc/file_table_backing.h"
#include "svc/disc/disc_geometry.h"
#include "svc/disc/disc_reader.h"
#include "svc/disc/frame_shaper.h"
#include "svc/disc/sector_header.h"
#include "svc/disc/data_sector_sealer.h"
#include "svc/image_opener.h"
#include "svc/msf.h"
#include "svc/disc/seek_span.h"
#include "svc/disc/subcode_interleaver.h"
#include "svc/disc/subcode_sidecar.h"
#include "svc/disc/toc.h"
#include "svc/disc/track.h"
#include "svc/types.h"
#include "svc/disc/chd_prefetch.h"
#include "svc/vfs.h"
#include "infra/seat.h"

namespace mister::svc::disc {

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

    [[nodiscard]] Ex<std::span<std::byte>> read_sector(Lba lba, std::span<std::byte> dst);

    [[nodiscard]] Ex<std::span<std::byte>> read_subcode(Lba lba, std::span<std::byte> dst);

    [[nodiscard]] Ex<std::span<std::byte>> read_user_data(Lba lba, std::span<std::byte> dst);

    [[nodiscard]] Ex<std::span<std::byte>> read_raw_frame(
        Lba lba,
        std::span<std::byte> dst,
        std::optional<SectorHeader> header = {}
    );

    [[nodiscard]] Ex<std::span<std::byte>> read_full_frame(Lba lba, std::span<std::byte> dst);

    static constexpr CddaOrder kWireCddaOrder = CddaOrder::LittleEndian;

    CddaOrder cdda_order() const noexcept { return cdda_order_; }

    const Toc& toc() const noexcept { return toc_; }

    bool mounted() const noexcept { return mounted_; }

    std::optional<TrackIndex> track_for_lba(Lba lba) const;

    SeekTime seek_time(SeekSpan span) const;

    static constexpr proto::BlockCount kAdviseAheadSectors{kFramesPerSecond};

    void advise_ahead(Lba lba) noexcept;

    std::uint32_t chd_hunk_bytes() const noexcept { return chd_.hunk_bytes(); }

    [[nodiscard]] DiscGeometry geometry() const noexcept {
        return DiscGeometry{.toc = toc_, .chd = chd_.geometry(), .mounted = mounted_};
    }

    [[nodiscard]] DiscCounters counters() const noexcept { return chd_.counters(); }

    std::uint32_t sync_decompress() const noexcept { return chd_.sync_decompress(); }

    std::uint32_t park_timeouts() const noexcept { return chd_.park_timeouts(); }

    std::uint32_t prefetch_refusals() const noexcept { return chd_.prefetch_refusals(); }

private:
    DiscEngine(CuePolicy p, ChdPrefetch& prefetch, DiscFileAccess files_io);

    enum class Form : std::uint8_t { Native, UserData, RedBook };

    struct FormShape {
        std::size_t out_len = 0;
        std::size_t dst_off = 0;
        std::uint32_t s_off = 0;
        std::size_t len = 0;
    };

    [[nodiscard]] static std::optional<FormShape> shape_of(
        Form form,
        std::uint32_t ss,
        TrackType type
    ) noexcept;

    [[nodiscard]] Ex<void> mount_cue(std::string_view cue_path);

    void open_sub_or_cdg_(std::string_view cue_path);
    [[nodiscard]] Ex<void> mount_iso(std::string_view iso_path);

    void hold_single_track_(
        TrackType type,
        proto::BlockSize sector_size,
        FileSize sz,
        std::unique_ptr<IFile> f
    );

    [[nodiscard]] Ex<void> declare_iso_2048(std::unique_ptr<IFile> f, FileSize sz);

    [[nodiscard]] Ex<void> mount_chd_path(std::string_view chd_path);
    [[nodiscard]] Ex<std::span<std::byte>> read_form(
        Lba lba,
        std::span<std::byte> dst,
        Form form,
        std::optional<SectorHeader> header = {}
    );

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

    std::optional<SubcodeInterleaver> block_shaper_;

    std::optional<DataSectorSealer> sector_sealer_;

    ChdBacking chd_;
};

}
