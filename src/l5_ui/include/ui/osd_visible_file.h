// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::ui {

class IOsdVisibleFile {
public:
    virtual ~IOsdVisibleFile() = default;

    virtual void on_visible(bool on) = 0;

protected:
    IOsdVisibleFile() = default;
    IOsdVisibleFile(const IOsdVisibleFile&) = default;
    IOsdVisibleFile& operator=(const IOsdVisibleFile&) = default;
};

}
