// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/link_frame.h"

namespace mister::proto::link {

class IFrameLane : public ILinkFrame {
public:
    virtual void settle() noexcept = 0;

    [[nodiscard]] virtual bool busy() const noexcept = 0;

    virtual void abandon() noexcept = 0;

    virtual void suspend() noexcept = 0;
    virtual void resume() noexcept = 0;

protected:
    IFrameLane() = default;
    IFrameLane(const IFrameLane&) = default;
    IFrameLane& operator=(const IFrameLane&) = default;
};

}
