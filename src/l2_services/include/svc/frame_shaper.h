// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include "proto/types.h"
#include "svc/track.h"

namespace mister::svc {

class IFrameShaper {
public:
    virtual ~IFrameShaper() = default;

    virtual void shape(proto::Lba lba, std::span<std::byte, kCdFrameSize> frame) const noexcept = 0;
};

}  // namespace mister::svc
