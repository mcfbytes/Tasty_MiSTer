// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <optional>

#include "infra/message_sum.h"
#include "proto/types.h"
#include "reactor/cause_set.h"

namespace mister::proto {

struct IrqCauseRow {
    IrqClass kind;
    reactor::Cause cause;
};

inline constexpr std::array<IrqCauseRow, infra::ordinal(IrqClass::kCount)> kIrqCauses{{
    {IrqClass::Blk, reactor::Cause::Blk},
    {IrqClass::Fifo, reactor::Cause::Fifo},
    {IrqClass::Ring, reactor::Cause::Ring},
    {IrqClass::CoreTick, reactor::Cause::CoreTick},
}};

consteval bool irq_causes_join() noexcept {
    const reactor::CauseSet not_doorbells = reactor::CauseSet{reactor::Cause::Tick}
        | reactor::CauseSet{reactor::Cause::Test}
        | reactor::CauseSet{reactor::Cause::Sys};
    reactor::CauseSet seen{};
    for (const IrqCauseRow& row : kIrqCauses) {
        if (row.cause == reactor::Cause::None || row.cause == reactor::Cause::kCount)
            return false;
        if (not_doorbells.has(row.cause) || seen.has(row.cause))
            return false;
        seen |= reactor::CauseSet{row.cause};
    }
    return infra::rows_are_ordinal(kIrqCauses);
}

static_assert(
    irq_causes_join(),
    "kIrqCauses: one row per IrqClass, in order, each a distinct doorbell Cause"
);

[[nodiscard]] constexpr std::optional<reactor::Cause> cause_of(IrqClass klass) noexcept {
    for (const IrqCauseRow& row : kIrqCauses) {
        if (row.kind == klass)
            return row.cause;
    }
    return std::nullopt;
}

}
