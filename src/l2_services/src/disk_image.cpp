// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/disk_image.h"

#include <array>

#include "infra/opt_ref.h"
#include "svc/card_writer.h"
#include "svc/raw_disk_image.h"

namespace mister::svc {

namespace {

[[nodiscard]] Ex<std::unique_ptr<IDiskImage>> bind_disk_image(
    Ex<std::unique_ptr<IFile>> file,
    std::string_view path,
    std::span<const DiskFormatRow> rows,
    OpenMode mode
) {
    if (!file)
        return std::unexpected(file.error());

    auto size = (*file)->size();
    if (!size)
        return std::unexpected(size.error());

    std::array<std::byte, kDiskMagicProbeBytes> head{};
    auto got = (*file)->read_at(proto::kFileStart, head);
    if (!got)
        return std::unexpected(got.error());

    const infra::OptRef<const DiskFormatRow>
        row = select_disk_format(rows, path, *size, std::span<const std::byte>(head).first(*got));
    if (!row) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    const bool writable = !opens_read_only(mode);
    switch (row->codec) {
        case DiskCodec::Raw:
            return std::unique_ptr<IDiskImage>(new RawDiskImage(std::move(*file), *size, writable));
        case DiskCodec::kCount:
            break;
    }
    return std::unexpected(
        Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(row->codec)}
    );
}

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

}
