// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "cores/disk_servant.h"
#include "cores/servant_grant.h"
#include "proto/disk_protocol.h"

namespace mister::cores {

struct CoreProfile;

using MakeDiskServant = std::unique_ptr<IDiskServant> (*)(const CoreProfile&, const ServantGrant&);

struct DiskServantRow {
    proto::DiskProtocol protocol{};
    MakeDiskServant make = nullptr;
};

}
