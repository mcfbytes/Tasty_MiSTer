// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>

#include "app/event.h"
#include "cores/progress.h"

namespace mister::app {

class InfoSink {
public:
    virtual ~InfoSink() = default;

    virtual void info(InfoId id, std::chrono::milliseconds timeout = {}) = 0;

    virtual void progress(InfoId id, cores::Progress p) = 0;

protected:
    InfoSink() = default;
    InfoSink(const InfoSink&) = default;
    InfoSink& operator=(const InfoSink&) = default;
};

}
