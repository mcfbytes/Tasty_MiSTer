// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/pad_masks.h"
#include "svc/types.h"

namespace mister::app::input {

class IPadLevelSink {
public:
    virtual ~IPadLevelSink() = default;

    [[nodiscard]] virtual bool emit(svc::PlayerIndex player, proto::PadMasks masks) noexcept = 0;

    [[nodiscard]] virtual bool release(svc::PlayerIndex player, svc::JoyMask tested) noexcept = 0;

protected:
    IPadLevelSink() = default;
    IPadLevelSink(const IPadLevelSink&) = default;
    IPadLevelSink& operator=(const IPadLevelSink&) = default;
};

}
