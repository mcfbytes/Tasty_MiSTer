// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

#include "infra/error.h"
#include "svc/vfs.h"

namespace mister::cores {

class HostFileSandbox {
public:
    HostFileSandbox(const svc::Vfs& vfs, std::string_view root) : vfs_(&vfs), root_(root) {}

    [[nodiscard]] Ex<std::string> resolve(std::string_view guest_path) const;

    const svc::Vfs& vfs() const noexcept { return *vfs_; }

private:
    const svc::Vfs* vfs_;
    std::string root_;
};

}
