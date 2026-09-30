// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/disk_image.h"

#include <array>

#include "svc/raw_disk_image.h"

namespace mister::svc {

Ex<std::unique_ptr<IDiskImage>> open_disk_image(const Vfs& vfs, std::string_view path,
                                                std::span<const DiskFormatRow> rows,
                                                OpenMode mode) {
    auto file = vfs.open(path, mode);
    if (!file) return std::unexpected(file.error());

    auto size = (*file)->size();
    if (!size) return std::unexpected(size.error());

    std::array<std::byte, kDiskMagicProbeBytes> head{};
    auto got = (*file)->read_at(0, head);
    if (!got) return std::unexpected(got.error());

    const DiskFormatRow* row =
        select_disk_format(rows, path, *size, std::span<const std::byte>(head).first(*got));
    if (row == nullptr) {
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
        Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(row->codec)});
}

}  // namespace mister::svc
