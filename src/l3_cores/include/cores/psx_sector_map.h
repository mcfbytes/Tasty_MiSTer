// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "svc/disc_geometry.h"

namespace mister::cores {

class PsxSectorMap {
    TASTY_SEAT_EXEMPT(component);

public:
    enum class Answer : std::uint8_t { Zeros, Filler, Pregap, Engine };

    explicit PsxSectorMap(const svc::DiscGeometry& disc) noexcept : disc_(disc) {}

    [[nodiscard]] Answer classify(std::int64_t lba) const noexcept;

    [[nodiscard]] static constexpr std::uint8_t fill_byte(Answer a) noexcept {
        return a == Answer::Zeros || a == Answer::Pregap ? 0x00 : 0xAA;
    }

private:
    const svc::DiscGeometry& disc_;
};

}
