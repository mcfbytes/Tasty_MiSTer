// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cores/types.h"

namespace mister::cores {

enum class StageOrder : std::uint8_t { None, MegaCd, PceCd, Psx, ThreeDo, Saturn, Cdi, NeoGeoCd };

struct SaveMountPolicy {
    IoIndex slot;

    std::optional<WideIoIndex> bracket{};

    std::span<const std::string_view> disc_words{};

    proto::FileSize pre_bytes{};

    bool pre_on_empty = false;

    std::string_view start_dir{};
};

struct StagingPolicy {
    StageOrder order = StageOrder::None;
    std::uint32_t reset_pulse_us = 0;

    IoIndex disc_slot{1};
    std::optional<SaveMountPolicy> save{};
};

}
