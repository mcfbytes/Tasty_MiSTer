// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "infra/prelude.h"
#include "svc/disk_format_row.h"
#include "svc/file.h"
#include "svc/sector_ask.h"
#include "svc/sector_geometry.h"
#include "svc/sector_read.h"
#include "svc/sector_types.h"

namespace mister::svc {

class CardWriter;
class Vfs;

class ISectorImage {
public:
    virtual ~ISectorImage() = default;

    [[nodiscard]] virtual SectorGeometry geometry() const noexcept = 0;
    [[nodiscard]] virtual bool writable() const noexcept = 0;

    [[nodiscard]] virtual Ex<SectorRead> read_sector(
        const SectorAsk& ask,
        std::span<std::byte> dst
    ) = 0;

    [[nodiscard]] virtual Ex<SectorRead> write_sector(
        const SectorAsk& ask,
        std::span<const std::byte> src
    ) = 0;

    [[nodiscard]] virtual Ex<void> clear() = 0;

protected:
    ISectorImage() = default;
    ISectorImage(const ISectorImage&) = default;
    ISectorImage& operator=(const ISectorImage&) = default;
};

[[nodiscard]] Ex<std::unique_ptr<ISectorImage>> open_sector_image(
    const Vfs& vfs,
    std::string_view path,
    std::span<const DiskFormatRow> rows,
    OpenMode mode,
    SectorSize headerless
);

[[nodiscard]] Ex<std::unique_ptr<ISectorImage>> open_sector_image(
    CardWriter& card,
    std::string_view path,
    std::span<const DiskFormatRow> rows,
    SectorSize headerless
);

}
