// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/sector_image.h"

#include <cstdint>
#include <utility>

#include "svc/atx_image.h"
#include "svc/disk_codec.h"
#include "svc/disk_format_pick.h"
#include "svc/card_writer.h"
#include "svc/flat_sector_image.h"
#include "svc/vfs.h"

namespace mister::svc {

namespace {

[[nodiscard]] Ex<std::unique_ptr<ISectorImage>> bind_sector_image(
    Ex<std::unique_ptr<IFile>> file,
    std::string_view path,
    std::span<const DiskFormatRow> rows,
    bool writable,
    SectorSize headerless
) {
    if (!file)
        return std::unexpected(file.error());
    const Ex<DiskFormatPick> pick = DiskFormatPick::of(**file, path, rows);
    if (!pick)
        return std::unexpected(pick.error());

    switch (pick->row.codec) {
        case DiskCodec::Raw:
            return FlatSectorImage::open(std::move(*file), writable, headerless);
        case DiskCodec::Atx:
            return AtxImage::open(std::move(*file), writable);

        case DiskCodec::Nib:
        case DiskCodec::GcrSectors:
        case DiskCodec::GcrTracks:
        case DiskCodec::T64:
        case DiskCodec::Woz525:
        case DiskCodec::Woz35:
        case DiskCodec::ProDosBlocks:
        case DiskCodec::Adf:
        case DiskCodec::kCount:
            break;
    }
    return std::unexpected(unserved_codec(pick->row.codec, ERR_SITE()));
}

}

Ex<std::unique_ptr<ISectorImage>> open_sector_image(
    const Vfs& vfs,
    std::string_view path,
    std::span<const DiskFormatRow> rows,
    OpenMode mode,
    SectorSize headerless
) {
    return bind_sector_image(vfs.open(path, mode), path, rows, !opens_read_only(mode), headerless);
}

Ex<std::unique_ptr<ISectorImage>> open_sector_image(
    CardWriter& card,
    std::string_view path,
    std::span<const DiskFormatRow> rows,
    SectorSize headerless
) {
    return bind_sector_image(card.open(path, OpenMode::ReadWrite), path, rows, true, headerless);
}

}
