// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/save_banks.h"

namespace mister::svc {

class BlankBankFilter final {
    TASTY_SEAT_RESIDENT(Io);

public:
    [[nodiscard]] proto::FileSize close(
        std::span<std::byte> image,
        const SaveBanks& banks
    ) const noexcept;
};

}
