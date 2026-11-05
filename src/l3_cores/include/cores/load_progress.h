// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/progress.h"

namespace mister::cores {

class ILoadProgress {
public:
    virtual ~ILoadProgress() = default;

    virtual void on_load_progress(Progress progress) noexcept = 0;

protected:
    ILoadProgress() = default;
    ILoadProgress(const ILoadProgress&) = default;
    ILoadProgress& operator=(const ILoadProgress&) = default;
};

}
