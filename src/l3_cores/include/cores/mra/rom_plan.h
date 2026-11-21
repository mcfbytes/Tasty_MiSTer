// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "cores/mra/fill_run.h"
#include "cores/mra/mra_map_word.h"
#include "cores/mra/mra_unit_len.h"
#include "cores/types.h"
#include "proto/types.h"
#include "svc/types.h"

namespace mister::cores::mra {

struct RomPlan {

    struct Segment {
        std::string path{};
        svc::Crc32 crc{};
        proto::FileOffset from{};
        std::shared_ptr<const std::vector<std::uint8_t>> text{};
        RomOffset base{};
        UnitLen unit{};
        MapWord map{};
        RomLength chunk{};
        RomLength len{};
    };

    struct Patch {
        RomOffset at{};
        std::vector<std::uint8_t> bytes{};
        bool xor_op = false;
    };

    using Item = std::variant<Segment, FillRun, Patch>;

    static constexpr auto kItemNames = std::to_array<std::string_view>(
        {"segment", "fill", "patch"}
    );

    std::vector<Item> items;
    RomLength length{};
};

static_assert(
    std::variant_size_v<RomPlan::Item> == RomPlan::kItemNames.size(),
    "RomPlan::Item: one name per arm"
);

}
