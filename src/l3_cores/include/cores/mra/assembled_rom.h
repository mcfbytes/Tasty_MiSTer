// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "cores/mra/fill_run.h"
#include "cores/mra/rom_bytes.h"
#include "cores/mra/rom_plan.h"
#include "cores/types.h"
#include "hal/types.h"
#include "proto/types.h"

namespace mister::cores::mra {

struct AssembledRom {

    std::optional<proto::WideIoIndex> index{};
    hal::FabricAddr address{};
    bool send = false;

    RomBytes bytes;
    std::vector<FillRun> runs;

    std::optional<RomPlan> plan;

    [[nodiscard]] RomLength length() const noexcept {
        if (plan)
            return plan->length;
        RomLength n = RomLength::of(bytes.view());
        for (const FillRun& r : runs) {
            n += r.len;
        }
        return n;
    }
};

}
