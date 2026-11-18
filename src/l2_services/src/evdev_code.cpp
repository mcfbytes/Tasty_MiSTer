// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/key_injector.h"
#include "svc/types.h"

namespace mister::svc {

void EvdevCode::inject_into(os::IKeyInjector& to, bool down) const noexcept {
    to.key(v, down);
}

}
