// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/snes/msu_data_loader.h"

#include "cores/snes/msu_wire.h"
#include "infra/prelude.h"

namespace mister::cores::snes {

namespace {

[[nodiscard]] Ex<void> put(
    ICoreWindow& w,
    proto::FileSize at,
    std::span<const std::uint8_t> bytes
) {
    auto n = w.write(at.file_word<std::size_t>(), std::as_bytes(bytes));
    if (!n)
        return std::unexpected(n.error());
    if (*n != bytes.size())
        return std::unexpected(at.refusal(Errc::aperture_range, ERR_SITE()));
    return {};
}

}

Ex<LoadPlan> MsuDataLoader::plan(const LoadAsk&, const proto::StatusWord&) {
    TASTY_SEAT_BODY(MsuDataLoader);
    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
}

Ex<void> MsuDataLoader::place(
    const TransferRow&,
    std::span<const std::uint8_t> piece,
    proto::FileOffset at,
    ICoreWindow& window
) {
    TASTY_SEAT_BODY(MsuDataLoader);
    return put(window, at - proto::kFileStart, piece);
}

}
