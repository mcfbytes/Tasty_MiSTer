// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::app {

class IOutputGeometry {
public:
    virtual ~IOutputGeometry() = default;
    IOutputGeometry(const IOutputGeometry&) = delete;
    IOutputGeometry& operator=(const IOutputGeometry&) = delete;

    [[nodiscard]] virtual std::uint16_t output_width() const noexcept = 0;
    [[nodiscard]] virtual std::uint16_t output_height() const noexcept = 0;

protected:
    IOutputGeometry() = default;
};

}  // namespace mister::app
