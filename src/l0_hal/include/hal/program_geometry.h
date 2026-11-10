// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <concepts>
#include <cstdint>
#include <tuple>

#include "infra/ascii.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "hal/fact.h"
#include "hal/fact_field.h"

namespace mister::hal {

struct ChunkBytes {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr std::uint32_t bytes() const noexcept { return v; }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    template <std::integral Word>
    [[nodiscard]] constexpr Word clamped(ChunkBytes lo, ChunkBytes hi) const noexcept {
        if (v < lo.v)
            return static_cast<Word>(lo.v);
        if (v > hi.v)
            return static_cast<Word>(hi.v);
        return static_cast<Word>(v);
    }

    friend constexpr bool operator==(ChunkBytes, ChunkBytes) = default;
    friend constexpr auto operator<=>(ChunkBytes, ChunkBytes) = default;
};

struct QuantumBytes {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr std::uint32_t bytes() const noexcept { return v; }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    friend constexpr bool operator==(QuantumBytes, QuantumBytes) = default;
    friend constexpr auto operator<=>(QuantumBytes, QuantumBytes) = default;
};

struct AlignBytes {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr std::uint32_t bytes() const noexcept { return v; }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    friend constexpr bool operator==(AlignBytes, AlignBytes) = default;
    friend constexpr auto operator<=>(AlignBytes, AlignBytes) = default;
};

struct ProgramGeometry {
    Fact<ChunkBytes> chunk_bytes;
    Fact<QuantumBytes> quantum;
    Fact<AlignBytes> align;

    [[nodiscard]] consteval bool is_valid(
        infra::ByteCount buffer_limit,
        AlignBytes required
    ) const noexcept;
};

class ProgramGeometryValues {

public:
    [[nodiscard]] static Ex<ProgramGeometryValues> resolve(
        const ProgramGeometry& g,
        infra::ErrorSite site
    ) noexcept;

    [[nodiscard]] static consteval ProgramGeometryValues measured(
        const ProgramGeometry& g
    ) noexcept;

    [[nodiscard]] static constexpr const auto& rows() noexcept { return kFields; }

    ChunkBytes chunk_bytes;
    QuantumBytes quantum;
    AlignBytes align;

private:
    static constexpr auto kFields = std::tuple{
        fact_row(
            FactField::ProgramChunkBytes,
            &ProgramGeometry::chunk_bytes,
            &ProgramGeometryValues::chunk_bytes
        ),
        fact_row(
            FactField::ProgramQuantum,
            &ProgramGeometry::quantum,
            &ProgramGeometryValues::quantum
        ),
        fact_row(FactField::ProgramAlign, &ProgramGeometry::align, &ProgramGeometryValues::align),
    };

    constexpr ProgramGeometryValues() noexcept = default;
};

static_assert(
    rows_are_distinct(ProgramGeometryValues::rows(), "program."),
    "each ProgramGeometry row names one program FactField, and its own"
);

consteval ProgramGeometryValues ProgramGeometryValues::measured(const ProgramGeometry& g) noexcept {
    ProgramGeometryValues values{};
    fill_rows_measured(values, kFields, g);
    return values;
}

consteval bool ProgramGeometry::is_valid(
    infra::ByteCount buffer_limit,
    AlignBytes required
) const noexcept {
    const ProgramGeometryValues g = ProgramGeometryValues::measured(*this);
    if (g.chunk_bytes.empty() || g.chunk_bytes.bytes() > buffer_limit.v)
        return false;
    if (g.quantum.empty() || g.chunk_bytes.bytes() % g.quantum.bytes() != 0u)
        return false;
    return !g.align.empty() && required.bytes() % g.align.bytes() == 0u;
}

[[nodiscard]] constexpr bool all_measured(const ProgramGeometry& g) noexcept {
    return every_row_measured(ProgramGeometryValues::rows(), g);
}

}
