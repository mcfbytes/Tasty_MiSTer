// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/ide/ide_geometry_rule.h"
#include "proto/types.h"
#include "svc/disk_geometry.h"

namespace mister::cores::ide {

class ImageGeometry final : public IGeometryRule {
public:
    [[nodiscard]] constexpr svc::DiskGeometry guess(proto::FileSize) const noexcept override {
        return {};
    }
};

inline constexpr ImageGeometry kImageGeometry{};

}
