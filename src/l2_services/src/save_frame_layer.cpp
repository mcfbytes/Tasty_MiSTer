// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/save_frame_layer.h"

#include <algorithm>

#include "infra/prelude.h"
#include "infra/span_cut.h"
#include "proto/types.h"

namespace mister::svc {

template <class Owner>
void SaveFrameLayerT<Owner>::lay_lead(
    std::span<std::byte> image,
    std::span<const std::byte> lead
) const noexcept {
    TASTY_SEAT_BODY(SeatOwner);
    if (!banks_.framed())
        return;
    const proto::FileSize whole = proto::FileSize::of(image);
    std::ranges::copy(infra::affine::first_clamped(lead, banks_.lead.min(whole)), image.begin());
}

template <class Owner>
void SaveFrameLayerT<Owner>::lay_heads(
    std::span<std::byte> image,
    std::span<const std::byte> heads
) const noexcept {
    TASTY_SEAT_BODY(SeatOwner);
    if (!banks_.framed())
        return;
    const proto::FileSize whole = proto::FileSize::of(image);
    for (proto::FileOffset at = proto::kFileStart + banks_.lead;
         at.remaining_in(whole) >= banks_.bank() && proto::FileSize::of(heads) >= banks_.head;
         at += banks_.bank()) {
        std::ranges::copy(
            infra::affine::first(heads, banks_.head),
            infra::affine::from(image, at).begin()
        );
        heads = infra::affine::from(heads, banks_.head);
    }
}

template class SaveFrameLayerT<seat_owner::Main>;
template class SaveFrameLayerT<seat_owner::Io>;

}
