// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "infra/seat.h"
#include "hal/fact.h"

namespace mister::hal {

struct ProgramGeometry {
    Fact<std::uint32_t> chunk_bytes;
    Fact<std::uint32_t> quantum;
    Fact<std::uint32_t> align;
};

constexpr bool all_measured(const ProgramGeometry& g) noexcept {
    return g.chunk_bytes.measured() && g.quantum.measured() && g.align.measured();
}

class ProgramGeometryValues {
    TASTY_SEAT_EXEMPT(component);

public:
    ProgramGeometryValues() = delete;

    [[nodiscard]] static Ex<ProgramGeometryValues> resolve(const ProgramGeometry& g,
                                                           std::uint16_t site) noexcept;

    [[nodiscard]] static consteval ProgramGeometryValues measured(const ProgramGeometry& g) {
        return ProgramGeometryValues{g.chunk_bytes.value(), g.quantum.value(), g.align.value()};
    }

    std::uint32_t chunk_bytes;
    std::uint32_t quantum;
    std::uint32_t align;

private:
    constexpr ProgramGeometryValues(std::uint32_t chunk, std::uint32_t q, std::uint32_t a) noexcept
        : chunk_bytes(chunk), quantum(q), align(a) {}
};

}  // namespace mister::hal
