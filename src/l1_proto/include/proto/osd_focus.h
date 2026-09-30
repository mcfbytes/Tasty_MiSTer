// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::proto {

class IOsdFocus {
public:
    virtual ~IOsdFocus() = default;

    [[nodiscard]] virtual bool osd_focused() const noexcept = 0;

protected:
    IOsdFocus() = default;
    IOsdFocus(const IOsdFocus&) = default;
    IOsdFocus& operator=(const IOsdFocus&) = default;
};

}  // namespace mister::proto
