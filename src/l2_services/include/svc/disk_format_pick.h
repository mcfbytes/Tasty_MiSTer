// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <string_view>

#include "infra/error.h"
#include "proto/types.h"
#include "svc/disk_format_row.h"
#include "svc/file.h"

namespace mister::svc {

struct DiskFormatPick {
    proto::FileSize size{};
    const DiskFormatRow& row;

    [[nodiscard]] static Ex<DiskFormatPick> of(
        IFile& file,
        std::string_view path,
        std::span<const DiskFormatRow> rows
    );
};

}
