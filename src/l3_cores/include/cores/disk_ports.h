// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "proto/disk_protocol.h"

namespace mister::cores {

class IDiskPort;

using DiskPorts = infra::
    IndexedArray<infra::OptRef<IDiskPort>, proto::DiskProtocol::kCount, proto::DiskProtocol>;

}
