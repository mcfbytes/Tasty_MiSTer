// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/event.h"
#include "infra/seat.h"

namespace mister::app {

class IEventSink {
public:
    virtual ~IEventSink() = default;

    virtual void deliver(const Event& e) = 0;

    virtual void deliver_from(const Event& e, SeatTag from) {
        (void)from;
        deliver(e);
    }

protected:
    IEventSink() = default;
    IEventSink(const IEventSink&) = default;
    IEventSink& operator=(const IEventSink&) = default;
};

}
