// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "cores/ide/ide_types.h"

namespace mister::cores::ide {

class IRemountAsk {
public:
    virtual ~IRemountAsk() = default;
    virtual void asked(
        RemountTarget target,
        IdeDriveSelect addressed,
        std::string_view name
    ) noexcept = 0;

protected:
    IRemountAsk() = default;
    IRemountAsk(const IRemountAsk&) = default;
    IRemountAsk& operator=(const IRemountAsk&) = default;
};

}
