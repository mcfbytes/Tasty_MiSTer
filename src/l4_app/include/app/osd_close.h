// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::app {

class IOsdClose {
public:
    virtual ~IOsdClose() = default;

    virtual void close_osd() noexcept = 0;

protected:
    IOsdClose() = default;
    IOsdClose(const IOsdClose&) = default;
    IOsdClose& operator=(const IOsdClose&) = default;
};

}  // namespace mister::app
