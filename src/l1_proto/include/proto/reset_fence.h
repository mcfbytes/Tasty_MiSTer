// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/status_word.h"

namespace mister::proto {

class IResetFence {
public:
    virtual ~IResetFence() = default;
    IResetFence() = default;
    IResetFence(const IResetFence&) = delete;
    IResetFence& operator=(const IResetFence&) = delete;

    virtual void before_status(const StatusWord& sent, const StatusWord& next) noexcept = 0;

    virtual void before_buttons(std::uint16_t sent, std::uint16_t next) noexcept = 0;

    virtual void before_reset_line() noexcept = 0;

protected:
    IResetFence(IResetFence&&) = default;
    IResetFence& operator=(IResetFence&&) = default;
};

}  // namespace mister::proto
