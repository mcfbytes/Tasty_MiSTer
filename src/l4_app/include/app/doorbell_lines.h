// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <optional>

#include "app/link_tx_reader.h"
#include "hal/cause_reg.h"
#include "hal/register_window.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "os/uio_handle.h"
#include "proto/irq_binding.h"

namespace mister::app {

inline constexpr std::size_t kMaxDoorbellRows = kLinkTxSlabBytes / sizeof(proto::IrqBinding);

struct DoorbellLines {
    TASTY_SEAT_EXEMPT(component);

    struct Line {
        proto::IrqBinding binding{};
        os::UioHandle handle{};
        std::optional<hal::RegisterWindow<hal::CauseReg>> cause{};
        std::optional<Errc> gate{};
        std::optional<Errc> why{};
    };

    std::array<Line, kMaxDoorbellRows> rows{};
    std::size_t count = 0;
};

}  // namespace mister::app
