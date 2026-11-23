// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/disk_image.h"

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/span_cut.h"
#include "infra/affine.h"
#include "svc/adf_codec.h"
#include "svc/card_writer.h"
#include "svc/disk_format_pick.h"
#include "svc/nib_codec.h"

#include "svc/gcr_sector_map.h"
#include "svc/gcr_track_image.h"
#include "svc/image_admission.h"
#include "svc/mirror_source.h"
#include "svc/raw_disk_image.h"
#include "svc/sector_mirror.h"
#include "svc/t64.h"
#include "svc/t64_image.h"
#include "svc/track_file_mirror.h"

namespace mister::svc {

namespace {

[[nodiscard]] Ex<std::unique_ptr<IDiskImage>> track_image_over(
    std::unique_ptr<IDiskImage> inner,
    GcrLayout layout
) {
    auto image = GcrTrackImage::open(std::move(inner), layout);
    if (!image)
        return std::unexpected(image.error());
    return std::unique_ptr<IDiskImage>(std::move(*image));
}

[[nodiscard]] Ex<std::unique_ptr<IDiskImage>> bind_disk_image(
    Ex<std::unique_ptr<IFile>> file,
    std::string_view path,
    std::span<const DiskFormatRow> rows,
    OpenMode mode
) {
    if (!file)
        return std::unexpected(file.error());
    const Ex<DiskFormatPick> pick = DiskFormatPick::of(**file, path, rows);
    if (!pick)
        return std::unexpected(pick.error());
    const proto::FileSize size = pick->size;

    const bool writable = !opens_read_only(mode);
    switch (pick->row.codec) {
        case DiskCodec::Raw:
            return std::unique_ptr<IDiskImage>(new RawDiskImage(std::move(*file), size, writable));
        case DiskCodec::Nib:
            return NibCodec::open(std::move(*file), writable);
        case DiskCodec::GcrSectors:
            return track_image_over(
                std::make_unique<RawDiskImage>(std::move(*file), size, writable),
                GcrLayout::Sectors
            );
        case DiskCodec::GcrTracks:
            return track_image_over(
                std::make_unique<RawDiskImage>(std::move(*file), size, writable),
                GcrLayout::Tracks
            );
        case DiskCodec::T64: {
            auto disk = T64Image::open(std::move(*file), size);
            if (!disk)
                return std::unexpected(disk.error());
            return track_image_over(std::move(*disk), GcrLayout::Sectors);
        }
        case DiskCodec::Adf:
            return AdfCodec::open(std::move(*file), size, writable);
        case DiskCodec::Woz525:
            return open_flux_image(FluxSide::Inch525, std::move(*file), path, size, writable);
        case DiskCodec::Woz35:
            return open_flux_image(FluxSide::Inch35, std::move(*file), path, size, writable);
        case DiskCodec::ProDosBlocks:
            return open_block_image(std::move(*file), path, size, writable);

        case DiskCodec::Atx:
        case DiskCodec::kCount:
            break;
    }
    return std::unexpected(
        Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(pick->row.codec)}
    );
}

}

Ex<std::vector<std::byte>> read_whole(IDiskImage& image) {
    std::vector<std::byte> bytes = image.size().zeroed<std::vector<std::byte>>();
    for (proto::FileSize got{}; got < image.size();) {
        auto n = image.read_at(proto::kFileStart + got, infra::affine::from(std::span{bytes}, got));
        if (!n)
            return std::unexpected(n.error());
        if (n->empty())
            return std::unexpected(Error{Errc::short_read, ERR_SITE(), got.detail()});
        got += proto::FileSize::of(*n);
    }
    return bytes;
}

[[nodiscard]] Ex<std::unique_ptr<IDiskImage>> open_disk_image(
    const Vfs& vfs,
    std::string_view path,
    std::span<const DiskFormatRow> rows,
    OpenMode mode
) {
    return bind_disk_image(vfs.open(path, mode), path, rows, mode);
}

[[nodiscard]] Ex<std::unique_ptr<IDiskImage>> open_disk_image(
    CardWriter& card,
    std::string_view path,
    std::span<const DiskFormatRow> rows
) {
    return bind_disk_image(card.open(path, OpenMode::ReadWrite), path, rows, OpenMode::ReadWrite);
}

namespace {

[[nodiscard]] proto::FileSize sector_image_bytes(proto::FileSize size) noexcept {
    const gcr::SectorMap map = gcr::SectorMap::of_image(size);
    return size.min(gcr::SectorMap::kSector.bytes_of(map.sectors()));
}

[[nodiscard]] Ex<std::vector<std::byte>> read_prefix(IFile& file, proto::FileSize want) {
    std::vector<std::byte> bytes = want.zeroed<std::vector<std::byte>>();
    auto got = file.read_prefix_at(proto::kFileStart, std::span<std::byte>{bytes});
    if (!got)
        return std::unexpected(got.error());
    bytes.resize(infra::affine::first(std::span<std::byte>{bytes}, *got).size());
    return bytes;
}

}

[[nodiscard]] Ex<std::unique_ptr<IMirrorSource>> open_mirror_source(
    const Vfs& vfs,
    std::string_view path,
    std::span<const DiskFormatRow> rows,
    proto::FileSize room
) {
    auto file = vfs.open(path, OpenMode::Read);
    if (!file)
        return std::unexpected(file.error());
    const Ex<DiskFormatPick> pick = DiskFormatPick::of(**file, path, rows);
    if (!pick)
        return std::unexpected(pick.error());
    switch (pick->row.codec) {
        case DiskCodec::GcrSectors: {
            auto sectors = read_prefix(**file, sector_image_bytes(pick->size));
            if (!sectors)
                return std::unexpected(sectors.error());
            return std::unique_ptr<IMirrorSource>{
                new SectorMirror{pick->size, std::move(*sectors)}
            };
        }
        case DiskCodec::GcrTracks:

            if (pick->size > room)
                return std::unexpected(pick->size.refusal(Errc::bad_format, ERR_SITE()));
            return std::unique_ptr<IMirrorSource>{new TrackFileMirror{
                std::move(*file),
                TrackFileMirrorExtent{.size = pick->size, .room = room}
            }};
        case DiskCodec::T64: {
            if (pick->size > t64::kMostTapeBytes)
                return std::unexpected(pick->size.refusal(Errc::bad_format, ERR_SITE()));
            auto tape = read_prefix(**file, pick->size);
            if (!tape)
                return std::unexpected(tape.error());
            std::optional<std::vector<std::byte>> disk = t64::to_d64(*tape);
            if (!disk)
                return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
            const proto::FileSize size = proto::FileSize::of(std::span<const std::byte>{*disk});
            return std::unique_ptr<IMirrorSource>{new SectorMirror{size, std::move(*disk)}};
        }
        case DiskCodec::Raw:
        case DiskCodec::Nib:
        case DiskCodec::Woz525:
        case DiskCodec::Woz35:
        case DiskCodec::ProDosBlocks:
        case DiskCodec::Atx:
        case DiskCodec::Adf:
        case DiskCodec::kCount:
            return std::unique_ptr<IMirrorSource>{};
    }
    return std::unique_ptr<IMirrorSource>{};
}

}
