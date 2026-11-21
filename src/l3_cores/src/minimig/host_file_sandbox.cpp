// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig/host_file_sandbox.h"

#include "infra/prelude.h"

namespace mister::cores::minimig {

Ex<std::string> HostFileSandbox::resolve(std::string_view guest_path) const {
    if (guest_path.empty()) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }

    return unimplemented(ERR_SITE());
}

}
