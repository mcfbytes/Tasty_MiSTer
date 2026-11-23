// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "cores/mac/mcda_toc.h"
#include "cores/mount_reads.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/disc/chd_prefetch.h"
#include "svc/disc/disc_engine.h"
#include "svc/file.h"
#include "svc/storage_backend.h"
#include "svc/vfs.h"

namespace mister::cores::mac {

class MacCdImage final : public svc::IStorageBackend {
    TASTY_SEAT_RESIDENT(Io);

public:
    MacCdImage(const svc::Vfs& vfs, infra::OptRef<MountReadsCell> reads) noexcept
        : vfs_(vfs),
          reads_cell_(reads) {}

    MacCdImage(const MacCdImage&) = delete;
    MacCdImage& operator=(const MacCdImage&) = delete;

    enum class Served : std::uint8_t {
        Nothing,
        Flat,
        Raw,
        Disc,
    };

    [[nodiscard]] Served served() const noexcept { return served_; }

    [[nodiscard]] Ex<infra::ByteCount> read_at(
        proto::FileOffset at,
        std::span<std::byte> dst
    ) override;

    [[nodiscard]] Ex<infra::ByteCount> write_at(
        proto::FileOffset at,
        std::span<const std::byte> src
    ) override;

    [[nodiscard]] Ex<proto::FileSize> open_extent(
        proto::SlotIndex slot,
        const svc::BlockCtl::StorageOrder& order
    ) override;
    void release(proto::SlotIndex slot) noexcept override;

private:
    struct Layout {
        proto::BlockSize sector{};
        proto::FileSize user_at{};
        bool mode2 = false;
    };

    [[nodiscard]] static constexpr proto::FileSize user_at_(
        bool mode2,
        proto::BlockSize sector
    ) noexcept;

    void close_() noexcept;
    [[nodiscard]] Ex<proto::FileSize> open_disc_(std::string_view path);
    [[nodiscard]] Ex<proto::FileSize> open_raw_(std::string_view path);
    [[nodiscard]] std::optional<Layout> probe_(svc::IFile& file) const;

    [[nodiscard]] proto::FileSize settle_(const svc::disc::Toc& toc);

    void read_data_(proto::FileOffset at, std::span<std::byte> dst);

    void read_audio_(proto::Lba frame, std::span<std::byte> dst);

    void read_sector_part_(proto::Lba sector, proto::FileSize from, std::span<std::byte> dst);
    void publish_() noexcept;

    const svc::Vfs& vfs_;
    infra::OptRef<MountReadsCell> reads_cell_;
    MountReads reads_{};

    Served served_ = Served::Nothing;
    std::unique_ptr<svc::IFile> file_;
    Layout layout_{};

    svc::disc::ChdPrefetch prefetch_;
    std::optional<svc::disc::DiscEngine> engine_;

    std::array<std::byte, kMcdaTocBytes> toc_block_{};
    std::optional<svc::disc::Track> data_track_;
    proto::BlockCount sectors_{};
    std::array<std::byte, svc::disc::kRawFrameBytes> scratch_{};
};

}
