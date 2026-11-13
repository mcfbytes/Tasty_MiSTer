// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <type_traits>

#include "cores/window_job_order.h"
#include "hal/types.h"
#include "infra/fixed_str.h"
#include "svc/slot_path.h"

namespace mister::cores::n64dd {

inline constexpr std::size_t kCoreDirChars = 32;
using CoreDir = FixedStr<kCoreDirChars, StrFit::Reject>;

struct RamSaveOrder {
    hal::FabricAddr load_addr{};
    svc::SlotPathText disk_path{};
    CoreDir core_dir{};

    [[nodiscard]] static std::optional<RamSaveOrder> of(
        hal::FabricAddr load_addr,
        std::string_view disk_path,
        const CoreDir& core_dir
    ) noexcept;
};

static_assert(std::is_trivially_copyable_v<RamSaveOrder> && WindowJobOrder::fits<RamSaveOrder>);

}
