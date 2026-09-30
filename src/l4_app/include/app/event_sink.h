// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/event.h"

namespace mister::app {

class IEventSink {
public:
    virtual ~IEventSink() = default;

    virtual void deliver(const Event& e) = 0;

protected:
    IEventSink() = default;
    IEventSink(const IEventSink&) = default;
    IEventSink& operator=(const IEventSink&) = default;
};

}  // namespace mister::app
