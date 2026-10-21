// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "ui/page.h"

namespace mister::ui {

class IConfirmHost {
public:
    virtual ~IConfirmHost() = default;

    virtual Transition on_confirm(std::uint8_t choice) = 0;

    virtual Transition on_cancel() { return Transition{Transition::Kind::Pop}; }

protected:
    IConfirmHost() = default;
    IConfirmHost(const IConfirmHost&) = default;
    IConfirmHost& operator=(const IConfirmHost&) = default;
};

}
