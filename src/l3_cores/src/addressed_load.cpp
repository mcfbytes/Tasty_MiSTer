// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/addressed_load.h"

#include <limits>

#include "hal/fabric_region.h"

namespace mister::cores {

std::optional<TransferRow> AddressedLoad::window_row(
    std::uint32_t addr,
    std::uint64_t bytes,
    proto::IoIndex index,
    const hal::PhysRegion& aperture
) noexcept {
    if (addr == 0 || bytes > std::numeric_limits<std::uint32_t>::max())
        return std::nullopt;

    if (!hal::resolve_load_region(aperture, os::PhysAddr{addr}, 1))
        return std::nullopt;
    TransferRow r{};
    r.dest = RowDest::Window;
    r.index = index;
    r.addr = addr;
    r.window = kPhysicalAddr;
    r.bracket = RowBracket::AroundWithLength;
    r.file_len = static_cast<std::uint32_t>(bytes);
    return r;
}

Ex<LoadPlan> AddressedLoad::plan(const LoadAsk& ask, const proto::StatusWord&) {
    TASTY_SEAT_BODY(AddressedLoad);
    LoadPlan p{};
    p.count = 1;

    if (ask.size == 0) {
        p.rows[0] = TransferRow{.index = ask.index};
        return p;
    }
    p.rows[0] = window_row(ask.load_addr, ask.size, ask.index, aperture_)
                    .value_or(TransferRow{.index = ask.index});
    return p;
}

void AddressedLoad::shape(const TransferRow&, std::span<std::uint8_t>, std::uint64_t) {
    TASTY_SEAT_BODY(AddressedLoad);
}

Ex<void> AddressedLoad::finish(ICoreWindow*) {
    TASTY_SEAT_BODY(AddressedLoad);
    return {};
}

std::unique_ptr<ILoader> make_addressed_load(const LoaderContext& ctx) {
    return std::make_unique<AddressedLoad>(ctx.aperture);
}

}
