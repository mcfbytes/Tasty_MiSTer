// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::app {

class IBracketClose {
public:
    virtual ~IBracketClose() = default;

    virtual void before_close() noexcept = 0;

protected:
    IBracketClose() = default;
    IBracketClose(const IBracketClose&) = default;
    IBracketClose& operator=(const IBracketClose&) = default;
};

}  // namespace mister::app
