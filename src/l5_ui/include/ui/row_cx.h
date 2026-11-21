// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/opt_ref.h"

namespace mister::ui {

class IOptionHost;

struct RowCx {
    infra::OptRef<IOptionHost> host{};
};

}
