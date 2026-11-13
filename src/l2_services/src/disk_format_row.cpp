// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/disk_format_row.h"

#include "infra/ascii.h"
#include "infra/path_parts.h"

namespace mister::svc {

namespace {

bool magic_hits(const DiskFormatRow& row, std::span<const std::byte> head) noexcept {
    if (row.magic.empty() || row.magic.size() > head.size())
        return false;
    for (std::size_t i = 0; i < row.magic.size(); ++i) {
        if (row.magic[i] != head[i])
            return false;
    }
    return true;
}

bool size_hits(const DiskFormatRow& row, proto::FileSize size) noexcept {
    if (row.size_bytes == 0)
        return false;
    const std::uint64_t want = row.size_bytes;
    const std::uint64_t got = size.v;
    const std::uint64_t delta = got > want ? got - want : want - got;
    return delta <= row.tolerance;
}

}

infra::OptRef<const DiskFormatRow> select_disk_format(
    std::span<const DiskFormatRow> rows,
    std::string_view path,
    proto::FileSize size,
    std::span<const std::byte> head
) noexcept {
    for (const DiskFormatRow& row : rows) {
        if (magic_hits(row, head))
            return infra::OptRef<const DiskFormatRow>{row};
    }
    for (const DiskFormatRow& row : rows) {
        if (size_hits(row, size))
            return infra::OptRef<const DiskFormatRow>{row};
    }
    const std::string_view ext = infra::extension_bare(path);
    for (const DiskFormatRow& row : rows) {
        if (row.ext.empty() || infra::ieq(row.ext, ext)) {
            return infra::OptRef<const DiskFormatRow>{row};
        }
    }
    return std::nullopt;
}

}
