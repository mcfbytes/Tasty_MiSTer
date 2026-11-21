// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/conf_switches.h"
#include "proto/status_word.h"

namespace mister::proto {

class IResetFence {
public:
    virtual ~IResetFence() = default;
    IResetFence() = default;
    IResetFence(const IResetFence&) = delete;
    IResetFence& operator=(const IResetFence&) = delete;

    virtual void before_status(const StatusWord& sent, const StatusWord& next) noexcept = 0;

    virtual void before_buttons(ConfSwitches sent, ConfSwitches next) noexcept = 0;

    virtual void before_reset_line() noexcept = 0;

protected:
    IResetFence(IResetFence&&) = default;
    IResetFence& operator=(IResetFence&&) = default;
};

}
