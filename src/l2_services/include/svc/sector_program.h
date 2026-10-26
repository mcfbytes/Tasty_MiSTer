// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "svc/disc_geometry.h"
#include "svc/disc_reader.h"

namespace mister::svc {

class ISectorProgram {
public:
    struct Source {
        bool engine = false;
        std::uint8_t fill = 0;
    };

    [[nodiscard]] virtual std::uint8_t slot() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t frame_bytes() const noexcept = 0;
    [[nodiscard]] virtual DiscForm form() const noexcept = 0;
    [[nodiscard]] virtual Source answer(
        const DiscGeometry& disc,
        std::int64_t lba
    ) const noexcept = 0;

protected:
    ISectorProgram() = default;
    ISectorProgram(const ISectorProgram&) = default;
    ISectorProgram& operator=(const ISectorProgram&) = default;
    ~ISectorProgram() = default;
};

}
