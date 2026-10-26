// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "svc/disc_geometry.h"
#include "svc/disc_reader.h"
#include "svc/sector_program.h"

namespace mister::cores {

class PsxSectorProgram final : public svc::ISectorProgram {
public:
    constexpr PsxSectorProgram() noexcept = default;

    [[nodiscard]] std::uint8_t slot() const noexcept override;
    [[nodiscard]] std::uint32_t frame_bytes() const noexcept override;
    [[nodiscard]] svc::DiscForm form() const noexcept override;
    [[nodiscard]] Source answer(
        const svc::DiscGeometry& disc,
        std::int64_t lba
    ) const noexcept override;
};

inline constexpr PsxSectorProgram kPsxSectorProgram{};

}
