// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/status_bar_facts.h"
#include "ui/system_facts.h"

namespace mister::ui {

class IHostSampler {
public:
    virtual ~IHostSampler() = default;

    virtual void sample(SystemFacts& sys, StatusBarFacts& bar) = 0;

protected:
    IHostSampler() = default;
    IHostSampler(const IHostSampler&) = default;
    IHostSampler& operator=(const IHostSampler&) = default;
};

}
