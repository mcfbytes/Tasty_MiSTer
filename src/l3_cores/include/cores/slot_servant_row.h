// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "cores/slot_servant_terms.h"
#include "proto/types.h"
#include "svc/storage_backend.h"

namespace mister::cores {

using MakeSlotServant = std::unique_ptr<svc::IStorageBackend> (*)(const SlotServantTerms&);

struct SlotServantRow {
    proto::SlotIndex slot{};
    MakeSlotServant make = nullptr;
};

}
