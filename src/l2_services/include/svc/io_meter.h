// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/types.h"

namespace mister::svc {

class IIoMeter {
public:
    IIoMeter() = default;
    IIoMeter(const IIoMeter&) = delete;
    IIoMeter& operator=(const IIoMeter&) = delete;
    IIoMeter(IIoMeter&&) = delete;
    IIoMeter& operator=(IIoMeter&&) = delete;

    virtual void read(proto::FileSize n) noexcept = 0;
    virtual void written(proto::FileSize n) noexcept = 0;
    virtual void flushed() noexcept = 0;

protected:
    ~IIoMeter() = default;
};

}
