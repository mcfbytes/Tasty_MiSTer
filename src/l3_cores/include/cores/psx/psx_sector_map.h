// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "svc/disc/disc_geometry.h"

namespace mister::cores::psx {

class PsxSectorMap {
    TASTY_SEAT_EXEMPT(component);

public:
    enum class Answer : std::uint8_t { Zeros, Filler, Pregap, Engine };

    explicit PsxSectorMap(const svc::disc::DiscGeometry& disc) noexcept : disc_(disc) {}

    [[nodiscard]] Answer classify(proto::Lba lba) const noexcept;

    [[nodiscard]] static constexpr std::uint8_t fill_byte(Answer a) noexcept {
        return a == Answer::Zeros || a == Answer::Pregap ? 0x00 : 0xAA;
    }

private:
    const svc::disc::DiscGeometry& disc_;
};

}
