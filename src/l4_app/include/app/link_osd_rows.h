// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::app {

class ILinkOsdRows {
public:
    virtual ~ILinkOsdRows() = default;

    virtual bool flush_one() noexcept = 0;

    [[nodiscard]] virtual unsigned backlog() const noexcept = 0;

protected:
    ILinkOsdRows() = default;
    ILinkOsdRows(const ILinkOsdRows&) = default;
    ILinkOsdRows& operator=(const ILinkOsdRows&) = default;
};

}
