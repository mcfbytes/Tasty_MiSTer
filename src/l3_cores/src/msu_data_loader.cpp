// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/msu_data_loader.h"

#include <algorithm>

#include "cores/msu_wire.h"

namespace mister::cores {

namespace {

[[nodiscard]] Ex<void> put(ICoreWindow& w, std::uint64_t at, std::span<const std::uint8_t> bytes) {
    auto n = w.write(static_cast<std::size_t>(at), std::as_bytes(bytes));
    if (!n) return std::unexpected(n.error());
    if (*n != bytes.size())
        return std::unexpected(
            Error{Errc::aperture_range, ERR_SITE(), static_cast<std::uint32_t>(at)});
    return {};
}

}  // namespace

Ex<LoadPlan> MsuDataLoader::plan(const LoadAsk&, const proto::StatusWord&) {
    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
}

Ex<void> MsuDataLoader::place(const TransferRow&, std::span<const std::uint8_t> piece,
                              std::uint64_t off, ICoreWindow& window) {
    if (off < msu::kGapAt) {
        const auto n =
            static_cast<std::size_t>(std::min<std::uint64_t>(piece.size(), msu::kGapAt - off));
        if (auto r = put(window, off, piece.first(n)); !r) return r;
        piece = piece.subspan(n);
        off += n;
    }
    if (piece.empty()) return {};
    return put(window, msu::window_offset(off), piece);
}

}  // namespace mister::cores
