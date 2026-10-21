// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "cores/staging_core.h"
#include "proto/disk_poll_decl.h"

namespace mister::hal {
class ISpiTransport;
}

namespace mister::cores {

class IDiskPort {
public:
    virtual ~IDiskPort() = default;

    virtual void service_rt(hal::ISpiTransport& link, const proto::DiskPollDecl& poll) noexcept = 0;

    virtual void order_bind(std::uint8_t unit, std::string_view path) noexcept = 0;

    [[nodiscard]] virtual MountState bind_answer(std::uint8_t unit) noexcept = 0;

    virtual void abandon_command() noexcept = 0;

    virtual void abandon_command_on(std::uint8_t unit) noexcept = 0;

protected:
    IDiskPort() = default;
    IDiskPort(const IDiskPort&) = default;
    IDiskPort& operator=(const IDiskPort&) = default;
};

}
