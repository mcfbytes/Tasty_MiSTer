// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/event.h"

namespace mister::app {

class InfoSink {
public:
    virtual ~InfoSink() = default;

    virtual void info(InfoId id, std::uint32_t timeout_ms = 0) = 0;

    virtual void progress(InfoId id, std::uint32_t cur, std::uint32_t max) = 0;

protected:
    InfoSink() = default;
    InfoSink(const InfoSink&) = default;
    InfoSink& operator=(const InfoSink&) = default;
};

}  // namespace mister::app
