// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

#include "cores/cheat_geometry.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct CheatCatalog {
    TASTY_SEAT_EXEMPT(const_shared);
    static constexpr std::size_t kMaxRows = 64;
    static constexpr std::size_t kNameChars = 127;
    static constexpr std::size_t kArenaBytes = 8192;

    struct ArenaSpan {
        std::uint16_t offset = 0;
        std::uint16_t len = 0;

        [[nodiscard]] constexpr std::size_t end() const noexcept {
            return std::size_t{offset} + len;
        }

        [[nodiscard]] std::span<const std::uint8_t> bytes(
            std::span<const std::uint8_t> arena
        ) const noexcept {
            return arena.subspan(offset, len);
        }
    };

    struct Row {

        FixedStr<kNameChars + 1, StrFit::Clip> name{};
        ArenaSpan body{};
    };

    cores::CheatGeometry geometry{};
    std::uint8_t count = 0;
    std::uint16_t used = 0;
    std::uint16_t dropped = 0;
    Row rows[kMaxRows] = {};
    std::uint8_t data[kArenaBytes] = {};

    [[nodiscard]] std::span<const std::uint8_t> body(const Row& row) const noexcept {
        return row.body.bytes(data);
    }

    [[nodiscard]] std::span<const Row> rows_view() const noexcept {
        return std::span<const Row>{rows}.first(std::min<std::size_t>(count, kMaxRows));
    }
};

static_assert(std::is_trivially_copyable_v<CheatCatalog>);

[[nodiscard]] inline bool cheat_catalog_add(
    CheatCatalog& out,
    std::string_view name,
    std::span<const std::uint8_t> bytes
) noexcept {
    const std::size_t at = out.used;
    if (out.count >= CheatCatalog::kMaxRows || bytes.size() > CheatCatalog::kArenaBytes - at) {
        ++out.dropped;
        return false;
    }
    CheatCatalog::Row& row = out.rows[out.count];
    (void)row.name.assign(name);
    row.body.offset = out.used;
    row.body.len = static_cast<std::uint16_t>(bytes.size());
    std::copy(bytes.begin(), bytes.end(), out.data + at);
    out.used = static_cast<std::uint16_t>(row.body.end());
    ++out.count;
    return true;
}

using CheatCatalogCell = xthread::Telemetry<CheatCatalog, SeatTag::Main>;

}
