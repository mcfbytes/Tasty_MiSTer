// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/blank_bank_filter.h"

#include <algorithm>

#include "infra/span_cut.h"

namespace mister::svc {

proto::FileSize BlankBankFilter::close(
    std::span<std::byte> image,
    const SaveBanks& banks
) const noexcept {
    TASTY_SEAT_BODY(BlankBankFilter);
    const proto::FileSize whole = proto::FileSize::of(image);
    if (!banks.framed() || whole < banks.lead)
        return whole;
    proto::FileOffset kept = proto::kFileStart + banks.lead;
    for (proto::FileOffset at = kept; at.remaining_in(whole) >= banks.bank(); at += banks.bank()) {
        const std::span<std::byte> bank = infra::affine::first(
            infra::affine::from(image, at),
            banks.bank()
        );
        const std::span<std::byte> body = infra::affine::from(bank, banks.head);
        if (std::ranges::all_of(body, [](std::byte b) { return b == kErasedFlash; }))
            continue;

        if (kept != at)
            std::ranges::copy(bank, infra::affine::from(image, kept).begin());
        kept += banks.bank();
    }
    return kept - proto::kFileStart;
}

}
