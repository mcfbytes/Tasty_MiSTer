// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/fb_terminal_host.h"

#include "infra/error.h"
#include "infra/prelude.h"

namespace mister::ui {

Ex<void> FbTerminalHost::launch(std::string_view) {
    return unimplemented(ERR_SITE());
}

Ex<bool> FbTerminalHost::reap() {
    return unimplemented(ERR_SITE());
}

}
