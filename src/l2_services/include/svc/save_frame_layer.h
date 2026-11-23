// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/seat_owner_io.h"
#include "infra/seat_owner_main.h"
#include "svc/save_banks.h"

namespace mister::svc {

template <class Owner>
class SaveFrameLayerT final {
    TASTY_SEAT_FROM(Owner);

public:
    explicit constexpr SaveFrameLayerT(const SaveBanks& banks) noexcept : banks_(banks) {}

    void lay_lead(std::span<std::byte> image, std::span<const std::byte> lead) const noexcept;

    void lay_heads(std::span<std::byte> image, std::span<const std::byte> heads) const noexcept;

private:
    SaveBanks banks_;
};

extern template class SaveFrameLayerT<seat_owner::Main>;
extern template class SaveFrameLayerT<seat_owner::Io>;

}
