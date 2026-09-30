// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::reactor {

class ICoreToken {
protected:
    ICoreToken() = default;
    ICoreToken(const ICoreToken&) = default;
    ICoreToken& operator=(const ICoreToken&) = default;
    ~ICoreToken() = default;
};

}  // namespace mister::reactor
