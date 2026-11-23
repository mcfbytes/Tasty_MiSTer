// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/ide/ide_geometry_rule.h"
#include "cores/x86/hdd_table.h"
#include "proto/types.h"
#include "svc/disk_geometry.h"

namespace mister::cores::x86 {

class HddTableRule final : public ide::IGeometryRule {
public:
    [[nodiscard]] constexpr svc::DiskGeometry guess(proto::FileSize image) const noexcept override {
        if (const auto row = hdd_shape_of_size(image))
            return svc::DiskGeometry{{}, row->heads, row->sectors};
        if (image > kMaxChsBytes)
            return svc::DiskGeometry{};
        return svc::DiskGeometry{{}, kFallbackHeads, kFallbackSectors};
    }

private:
    static constexpr svc::DiskGeometry::Heads kFallbackHeads{16};
    static constexpr svc::DiskGeometry::Sectors kFallbackSectors{63};
};

inline constexpr HddTableRule kHddTableRule{};

}
