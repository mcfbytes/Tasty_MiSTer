// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "cores/companion_ask.h"
#include "cores/companion_plan.h"
#include "cores/loader.h"
#include "cores/mailbox_servants.h"
#include "infra/error.h"
#include "svc/vfs.h"

namespace mister::cores {

class MsuCompanion;

using MakeCompanion = std::unique_ptr<MsuCompanion> (*)(const svc::Vfs&);

}
