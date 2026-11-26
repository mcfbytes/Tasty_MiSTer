// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hal/ocram_run_edge.h"

namespace mister::hal {

class IOcramRun {
public:
    virtual ~IOcramRun() = default;
    IOcramRun() = default;
    IOcramRun(const IOcramRun&) = delete;
    IOcramRun& operator=(const IOcramRun&) = delete;

protected:
    IOcramRun(IOcramRun&&) = default;
    IOcramRun& operator=(IOcramRun&&) = default;

public:
    virtual void ocram_run(OcramRunEdge edge) = 0;
};

}
