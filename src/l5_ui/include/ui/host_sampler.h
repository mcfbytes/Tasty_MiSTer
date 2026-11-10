// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/host_sample.h"
#include "ui/status_bar_facts.h"

namespace mister::ui {

class IHostSampler {
public:
    virtual ~IHostSampler() = default;

    [[nodiscard]] virtual HostSample snapshot() = 0;

protected:
    IHostSampler() = default;
    IHostSampler(const IHostSampler&) = default;
    IHostSampler& operator=(const IHostSampler&) = default;
};

}
