// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cores/types.h"

namespace mister::cores {

struct SaveMountPolicy {
    IoIndex slot;

    std::optional<WideIoIndex> bracket{};

    std::span<const std::string_view> disc_words{};

    proto::FileSize pre_bytes{};

    bool pre_on_empty = false;

    std::string_view start_dir{};
};

struct StagingPolicy {

    bool staged = false;
    std::uint32_t reset_pulse_us = 0;

    IoIndex disc_slot{1};
    std::optional<SaveMountPolicy> save{};
};

}
