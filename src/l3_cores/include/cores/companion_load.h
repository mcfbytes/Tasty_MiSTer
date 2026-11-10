// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "cores/companion.h"

namespace mister::svc {
class Vfs;
}

namespace mister::cores {

using MakeCompanion = std::unique_ptr<ICompanion> (*)(const svc::Vfs&);

}
