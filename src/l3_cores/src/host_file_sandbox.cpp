// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/host_file_sandbox.h"

namespace mister::cores {

Ex<std::string> HostFileSandbox::resolve(std::string_view guest_path) const {
    if (guest_path.empty()) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }

    return unimplemented(ERR_SITE());
}

}
