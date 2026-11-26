// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::hal {

class IOcramPlatform {
public:
    virtual ~IOcramPlatform() = default;
    IOcramPlatform() = default;
    IOcramPlatform(const IOcramPlatform&) = delete;
    IOcramPlatform& operator=(const IOcramPlatform&) = delete;

protected:
    IOcramPlatform(IOcramPlatform&&) = default;
    IOcramPlatform& operator=(IOcramPlatform&&) = default;

public:
    [[nodiscard]] virtual bool f2h_bridge_released() const = 0;

    virtual void disarm_warm_boot() = 0;

    virtual void clear_ecc_flags() = 0;
};

}
