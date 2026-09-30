// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::app {

class IOwnerTick {
public:
    virtual ~IOwnerTick() = default;
    virtual void tick() noexcept = 0;

protected:
    IOwnerTick() = default;
    IOwnerTick(const IOwnerTick&) = default;
    IOwnerTick& operator=(const IOwnerTick&) = default;
};

}  // namespace mister::app
