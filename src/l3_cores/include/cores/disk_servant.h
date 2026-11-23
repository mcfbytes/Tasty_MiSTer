// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "cores/mount_state.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/xthread/wake_flag.h"
#include "proto/disk_act.h"
#include "proto/disk_protocol.h"
#include "proto/types.h"
#include "proto/user_io/poll_frame.h"

namespace mister::svc {
class Vfs;
}

namespace mister::cores {

class IDiskServant {
public:
    virtual ~IDiskServant() = default;

    [[nodiscard]] virtual MountState bind(proto::DiskUnit unit, std::string_view path) noexcept = 0;

    [[nodiscard]] virtual proto::DiskAct start(
        const proto::user_io::PollFrame& block,
        std::span<std::byte, proto::kDiskActBytes> bytes
    ) noexcept = 0;

    [[nodiscard]] virtual proto::DiskAct resume(
        std::span<std::byte, proto::kDiskActBytes> bytes
    ) noexcept = 0;

    virtual void release() noexcept = 0;

    [[nodiscard]] virtual proto::DiskAct standing() const noexcept {
        return proto::DiskAct::none();
    }

protected:
    IDiskServant() = default;
    IDiskServant(const IDiskServant&) = default;
    IDiskServant& operator=(const IDiskServant&) = default;
};

using DiskServantSet = infra::
    IndexedArray<std::unique_ptr<IDiskServant>, proto::DiskProtocol::kCount, proto::DiskProtocol>;

}
