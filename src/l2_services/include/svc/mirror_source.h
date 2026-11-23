// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "proto/types.h"
#include "svc/disk_format_row.h"
#include "svc/mirror_piece.h"

namespace mister::svc {

class Vfs;

class IMirrorSource {
public:
    virtual ~IMirrorSource() = default;

    [[nodiscard]] virtual Ex<std::optional<MirrorPiece>> next() = 0;

protected:
    IMirrorSource() = default;
    IMirrorSource(const IMirrorSource&) = default;
    IMirrorSource& operator=(const IMirrorSource&) = default;
};

[[nodiscard]] Ex<std::unique_ptr<IMirrorSource>> open_mirror_source(
    const Vfs& vfs,
    std::string_view path,
    std::span<const DiskFormatRow> rows,
    proto::FileSize room
);

}
