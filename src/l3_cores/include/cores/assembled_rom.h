// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cores/fill_run.h"
#include "cores/rom_bytes.h"

namespace mister::cores::mra {

struct AssembledRom {
    int index = 0;
    std::uint32_t address = 0;
    bool send = false;

    RomBytes bytes;
    std::vector<FillRun> runs;

    [[nodiscard]] std::size_t size() const noexcept {
        std::size_t n = bytes.size();
        for (const FillRun& r : runs) {
            n += r.len;
        }
        return n;
    }
};

}
