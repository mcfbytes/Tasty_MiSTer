// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

#include "infra/ascii.h"
#include "proto/save_trigger.h"
#include "proto/status_bit.h"

namespace mister::cores {

struct SaveProbeRow {
    std::string_view core;
    proto::StatusBit bit;
    bool ex = false;

    bool self_gated = false;

    [[nodiscard]] constexpr bool matches(const proto::SaveTrigger& t) const noexcept {
        return t.bit == bit && t.ex == ex;
    }

    friend constexpr bool operator==(const SaveProbeRow&, const SaveProbeRow&) = default;
};

inline constexpr auto kSaveProbeRows = std::to_array<SaveProbeRow>({
    {.core = "3DO", .bit = proto::StatusBit{13}, .ex = false},
    {.core = "GBA", .bit = proto::StatusBit{13}, .ex = false},
    {.core = "GAMEBOY", .bit = proto::StatusBit{10}, .ex = false},
    {.core = "Jaguar", .bit = proto::StatusBit{11}, .ex = false},
    {.core = "Loopy", .bit = proto::StatusBit{16}, .ex = false},
    {.core = "MegaCD", .bit = proto::StatusBit{17}, .ex = false},
    {.core = "MegaDrive", .bit = proto::StatusBit{17}, .ex = false},
    {.core = "NES", .bit = proto::StatusBit{7}, .ex = false},
    {.core = "NEOGEO", .bit = proto::StatusBit{12}, .ex = false, .self_gated = true},
    {.core = "PSX", .bit = proto::StatusBit{13}, .ex = false},
    {.core = "PokemonMini", .bit = proto::StatusBit{10}, .ex = false},
    {.core = "S32X", .bit = proto::StatusBit{17}, .ex = false},
    {.core = "SGB", .bit = proto::StatusBit{13}, .ex = false},
    {.core = "SMS", .bit = proto::StatusBit{7}, .ex = false},
    {.core = "SNES", .bit = proto::StatusBit{13}, .ex = false},
    {.core = "Saturn", .bit = proto::StatusBit{25}, .ex = false},
    {.core = "SuperAcan", .bit = proto::StatusBit{13}, .ex = false},
    {.core = "TGFX16", .bit = proto::StatusBit{7}, .ex = false},
    {.core = "VirtualBoy", .bit = proto::StatusBit{10}, .ex = false},
});

[[nodiscard]] constexpr std::optional<SaveProbeRow> find_save_probe(
    std::string_view core_name
) noexcept {
    for (const SaveProbeRow& r : kSaveProbeRows) {
        if (infra::ieq(r.core, core_name))
            return r;
    }
    return std::nullopt;
}

namespace detail {

consteval bool save_probe_names_distinct() {
    for (std::size_t i = 0; i < kSaveProbeRows.size(); ++i) {
        if (kSaveProbeRows[i].core.empty())
            return false;
        for (std::size_t j = i + 1; j < kSaveProbeRows.size(); ++j) {
            if (infra::ieq(kSaveProbeRows[i].core, kSaveProbeRows[j].core))
                return false;
        }
    }
    return true;
}

}

static_assert(detail::save_probe_names_distinct(), "one row per core name, compared without case");

}
