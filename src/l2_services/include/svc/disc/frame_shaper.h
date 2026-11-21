// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include "proto/types.h"
#include "svc/disc/track.h"

namespace mister::svc::disc {

class IFrameShaper {
public:
    virtual ~IFrameShaper() = default;

    virtual void shape(
        proto::Lba lba,
        std::span<std::byte, kFullFrameBytes> frame
    ) const noexcept = 0;
};

}
