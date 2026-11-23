// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "proto/types.h"
#include "svc/disk_geometry.h"
#include "svc/vhd_sidecar.h"

namespace mister::cores::ide {

struct IdeMedia {
    svc::DiskGeometry geometry{};
    proto::BlockCount total{};

    [[nodiscard]] static IdeMedia of(
        proto::FileSize image,
        const svc::DiskGeometry& guess,
        const std::optional<svc::VhdSidecar>& sidecar
    ) noexcept {
        if (sidecar) {
            if (const auto total = svc::apply_vhd_sidecar(*sidecar, image)) {
                const svc::DiskGeometry named{{}, sidecar->heads, sidecar->sectors};
                return IdeMedia{named.addressing(image), *total};
            }
        }
        return IdeMedia{guess.addressing(image), svc::DiskGeometry::kSector.blocks_in(image)};
    }

    friend constexpr bool operator==(const IdeMedia&, const IdeMedia&) = default;
};

}
