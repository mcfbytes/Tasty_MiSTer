// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "os/connect/hci_index.h"

namespace mister::os::connect {

enum class HciResetOutcome : std::uint8_t {
    Reset,
    NoAdapter,
};

class HciReset {
    TASTY_SEAT_RESIDENT(Connect);

public:
    static constexpr int kProtocolHci = 1;
    static constexpr unsigned kRequestDevReset = 203;

    [[nodiscard]] static Ex<HciResetOutcome> reset(HciIndex index) noexcept;
};

}
