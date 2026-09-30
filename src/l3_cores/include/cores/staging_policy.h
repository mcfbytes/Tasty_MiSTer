// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "proto/types.h"

namespace mister::cores {

using proto::IoIndex;
using proto::WideIoIndex;

enum class StageOrder : std::uint8_t { None, MegaCd, PceCd, Psx, ThreeDo, Saturn, Cdi, NeoGeoCd };

struct StagingPolicy {
    StageOrder order = StageOrder::None;
    std::uint32_t reset_pulse_us = 0;

    IoIndex disc_slot{1};
    IoIndex save_slot{0};

    WideIoIndex save_dest{};

    std::span<const std::string_view> save_name_disc_words{};

    std::uint64_t save_pre_bytes = 0;

    std::string_view start_save_dir{};
};

}  // namespace mister::cores
