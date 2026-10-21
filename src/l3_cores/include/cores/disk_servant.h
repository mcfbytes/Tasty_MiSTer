// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "cores/staging_core.h"
#include "proto/disk_act.h"

namespace mister::svc {
class Vfs;
}

namespace mister::cores {

class IDiskServant {
public:
    virtual ~IDiskServant() = default;

    [[nodiscard]] virtual MountState bind(std::uint8_t unit, std::string_view path) noexcept = 0;

    [[nodiscard]] virtual proto::DiskAct start(
        std::span<const std::byte, proto::kDiskStateBytes> state,
        std::span<std::byte, proto::kDiskActBytes> bytes
    ) noexcept = 0;

    [[nodiscard]] virtual proto::DiskAct resume(
        std::span<std::byte, proto::kDiskActBytes> bytes
    ) noexcept = 0;

    virtual void release() noexcept = 0;

protected:
    IDiskServant() = default;
    IDiskServant(const IDiskServant&) = default;
    IDiskServant& operator=(const IDiskServant&) = default;
};

[[nodiscard]] std::unique_ptr<IDiskServant> make_disk_servant(const svc::Vfs& vfs);

}
