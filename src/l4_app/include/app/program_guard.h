// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::app {

class IProgramGuard {
public:
    virtual ~IProgramGuard() = default;
    IProgramGuard() = default;
    IProgramGuard(const IProgramGuard&) = delete;
    IProgramGuard& operator=(const IProgramGuard&) = delete;

protected:
    IProgramGuard(IProgramGuard&&) = default;
    IProgramGuard& operator=(IProgramGuard&&) = default;

public:
    virtual void proof() = 0;
};

}
