// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/types.h"
#include "svc/disk_geometry.h"

namespace mister::cores::ide {

class IGeometryRule {
public:
    virtual ~IGeometryRule() = default;

    [[nodiscard]] virtual svc::DiskGeometry guess(proto::FileSize image) const noexcept = 0;

protected:
    IGeometryRule() = default;
    IGeometryRule(const IGeometryRule&) = default;
    IGeometryRule& operator=(const IGeometryRule&) = default;
};

}
