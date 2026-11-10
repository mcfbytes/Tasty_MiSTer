// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/output_size.h"

namespace mister::app {

class IOutputGeometry {
public:
    virtual ~IOutputGeometry() = default;
    IOutputGeometry(const IOutputGeometry&) = delete;
    IOutputGeometry& operator=(const IOutputGeometry&) = delete;

    [[nodiscard]] virtual OutputSize output_size() const noexcept = 0;

protected:
    IOutputGeometry() = default;
};

}
