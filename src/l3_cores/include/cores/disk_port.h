// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "cores/mount_state.h"
#include "proto/disk_poll_decl.h"
#include "proto/types.h"

namespace mister::proto {
class Link;
}

namespace mister::cores {

class IDiskPort {
public:
    virtual ~IDiskPort() = default;

    virtual void service_rt(proto::Link& link, const proto::DiskPollDecl& poll) noexcept = 0;

    virtual void order_bind(proto::DiskUnit unit, std::string_view path) noexcept = 0;

    [[nodiscard]] virtual MountState bind_answer(proto::DiskUnit unit) noexcept = 0;

    virtual void abandon_command() noexcept = 0;

    virtual void abandon_command_on(proto::DiskUnit unit) noexcept = 0;

protected:
    IDiskPort() = default;
    IDiskPort(const IDiskPort&) = default;
    IDiskPort& operator=(const IDiskPort&) = default;
};

}
