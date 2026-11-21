// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/types.h"
#include "svc/disc/disc_geometry.h"
#include "svc/disc/disc_reader.h"
#include "svc/disc/sector_program.h"

namespace mister::cores::psx {

class PsxSectorProgram final : public svc::disc::ISectorProgram {
public:
    constexpr PsxSectorProgram() noexcept = default;

    [[nodiscard]] proto::SlotIndex slot() const noexcept override;
    [[nodiscard]] proto::BlockSize frame_bytes() const noexcept override;
    [[nodiscard]] svc::disc::DiscForm form() const noexcept override;
    [[nodiscard]] Source answer(
        const svc::disc::DiscGeometry& disc,
        proto::Lba lba
    ) const noexcept override;
};

inline constexpr PsxSectorProgram kPsxSectorProgram{};

}
