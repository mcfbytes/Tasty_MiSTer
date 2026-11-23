// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/disk_format_pick.h"

#include <array>
#include <cstddef>

#include "infra/opt_ref.h"

namespace mister::svc {

Ex<DiskFormatPick> DiskFormatPick::of(
    IFile& file,
    std::string_view path,
    std::span<const DiskFormatRow> rows
) {
    const auto size = file.size();
    if (!size)
        return std::unexpected(size.error());
    std::array<std::byte, kDiskMagicProbeBytes> head{};
    const auto got = file.read_at(proto::kFileStart, head);
    if (!got)
        return std::unexpected(got.error());
    const infra::OptRef<const DiskFormatRow> row = select_disk_format(
        rows,
        path,
        *size,
        got->first_in(std::span<const std::byte>(head))
    );
    if (!row)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    return DiskFormatPick{.size = *size, .row = *row};
}

}
