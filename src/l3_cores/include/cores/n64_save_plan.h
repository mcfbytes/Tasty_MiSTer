// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "cores/n64_memory_type.h"
#include "cores/n64_save_layout.h"
#include "cores/nvram_store.h"
#include "cores/save_row_count.h"
#include "cores/types.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::cores::n64 {

struct SavePlan {
    TASTY_SEAT_EXEMPT(component);

    struct Row {

        proto::FileSize length{};
        bool byteswap = false;
    };

    SavePath stock{};
    infra::IndexedArray<Row, kMaxSaveRows, SaveRow> rows{};

    [[nodiscard]] constexpr SaveLayout layout() const noexcept {
        SaveLayout out{};
        std::uint32_t off = 0;
        for (std::uint8_t i = 0; i < kMaxSaveRows; ++i) {

            const auto len = static_cast<std::uint32_t>(rows[SaveRow{i}].length.v);
            if (len == 0)
                break;
            out.rows[SaveRow{i}] = RowExtent{off, len};
            off += len;
            out.count = SaveRowCount::of(i + 1u);
        }
        out.valid = !out.count.none();
        return out;
    }
};

static_assert(std::is_trivially_copyable_v<SavePlan>);
static_assert(sizeof(SavePlan) == 344);

}
