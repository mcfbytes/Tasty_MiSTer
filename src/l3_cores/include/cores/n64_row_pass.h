// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "cores/core_window.h"
#include "cores/n64_load_facts.h"
#include "cores/transfer_row.h"
#include "infra/error.h"
#include "infra/opt_ref.h"

namespace mister::cores::n64 {

enum class PiecePlace : std::uint8_t { First, Later };

class IRowPass {
public:
    virtual ~IRowPass() = default;

    virtual void shape(
        const TransferRow& row,
        LoadFacts& slab,
        std::span<std::uint8_t> piece,
        PiecePlace at
    ) = 0;

    [[nodiscard]] virtual Ex<void> place(std::span<const std::uint8_t>, ICoreWindow&) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    [[nodiscard]] virtual Ex<bool> conclude(ICoreWindow&) { return false; }

    [[nodiscard]] virtual Ex<bool> finish(LoadFacts& slab, infra::OptRef<ICoreWindow> window) = 0;
};

}
