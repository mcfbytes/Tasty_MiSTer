// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "cores/core_profile.h"
#include "cores/descriptor_io.h"
#include "cores/sector_io.h"

namespace mister::cores {

struct CdIoIndices {
    IoIndex data;
    std::optional<IoIndex> cdda;
    std::optional<IoIndex> subcode;

    std::optional<IoIndex> info;

    [[nodiscard]] constexpr std::optional<SectorIo> sectors() const noexcept {
        if (!cdda || !subcode)
            return std::nullopt;
        return SectorIo{data, *cdda, *subcode};
    }

    [[nodiscard]] constexpr std::optional<DescriptorIo> descriptor() const noexcept {
        if (!info)
            return std::nullopt;
        return DescriptorIo{data, *info};
    }
};

}
