// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "svc/nic_mac.h"

namespace mister::cores::minimig {

class ILanceEvents {
public:
    virtual ~ILanceEvents() = default;

    virtual void initialised(const svc::MacAddress& station) noexcept = 0;

    virtual void transmit_demand() noexcept = 0;

    virtual void interrupt() noexcept = 0;

protected:
    ILanceEvents() = default;
    ILanceEvents(const ILanceEvents&) = default;
    ILanceEvents& operator=(const ILanceEvents&) = default;
};

}
