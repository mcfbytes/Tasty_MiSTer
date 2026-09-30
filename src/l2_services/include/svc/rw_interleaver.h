// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"
#include "svc/frame_shaper.h"

namespace mister::svc {

class RwInterleaver final : public IFrameShaper {
    TASTY_SEAT_RESIDENT(Io);

public:
    void shape(proto::Lba lba, std::span<std::byte, kCdFrameSize> frame) const noexcept override;
};

}  // namespace mister::svc
