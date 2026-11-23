// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include "cores/ide/ide_task_file.h"
#include "cores/ide/ide_types.h"
#include "hal/types.h"

namespace mister::cores::ide {

class IIdeTransport {
public:
    virtual ~IIdeTransport() = default;

    virtual void write_task_file(const IdeTaskFile& regs) noexcept = 0;

    virtual void send_data(std::span<const std::byte> data) noexcept = 0;

    virtual void receive_data(std::span<std::byte> into) noexcept = 0;

    virtual void write_reg(IdeRegister reg, hal::SpiWord value) noexcept = 0;

protected:
    IIdeTransport() = default;
    IIdeTransport(const IIdeTransport&) = default;
    IIdeTransport& operator=(const IIdeTransport&) = default;
};

}
