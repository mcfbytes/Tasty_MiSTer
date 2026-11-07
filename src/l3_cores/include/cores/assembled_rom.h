// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <vector>

#include "cores/fill_run.h"
#include "cores/rom_bytes.h"
#include "cores/types.h"
#include "hal/types.h"

namespace mister::cores::mra {

struct AssembledRom {
    int index = 0;
    hal::FabricAddr address{};
    bool send = false;

    RomBytes bytes;
    std::vector<FillRun> runs;

    [[nodiscard]] RomLength length() const noexcept {
        RomLength n = RomLength::of(bytes.view());
        for (const FillRun& r : runs) {
            n += r.len;
        }
        return n;
    }
};

}
