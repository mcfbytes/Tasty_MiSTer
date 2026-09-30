// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "cores/core_window.h"
#include "cores/load_ask.h"
#include "cores/load_plan.h"
#include "infra/error.h"
#include "proto/status_word.h"

namespace mister::cores {

class ILoader {
public:
    virtual ~ILoader() = default;

    [[nodiscard]] virtual Ex<LoadPlan> plan(const LoadAsk& ask,
                                            const proto::StatusWord& status) = 0;

    virtual void shape(const TransferRow& row, std::span<std::uint8_t> piece,
                       std::uint64_t off) = 0;

    [[nodiscard]] virtual Ex<void> place(const TransferRow& row,
                                         std::span<const std::uint8_t> piece, std::uint64_t off,
                                         ICoreWindow& window);

    [[nodiscard]] virtual Ex<bool> conclude(const TransferRow& row, ICoreWindow& window);

    [[nodiscard]] virtual Ex<void> finish(ICoreWindow* window) = 0;

    [[nodiscard]] virtual std::span<const std::uint8_t> facts() const noexcept = 0;

protected:
    ILoader() = default;
    ILoader(const ILoader&) = default;
    ILoader& operator=(const ILoader&) = default;
};

[[nodiscard]] inline Ex<void> ILoader::place(const TransferRow&, std::span<const std::uint8_t>,
                                             std::uint64_t, ICoreWindow&) {
    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
}
[[nodiscard]] inline Ex<bool> ILoader::conclude(const TransferRow&, ICoreWindow&) { return false; }

}  // namespace mister::cores
