// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::app {

class IWriteVerdict {
public:
    virtual ~IWriteVerdict() = default;
    IWriteVerdict(const IWriteVerdict&) = delete;
    IWriteVerdict& operator=(const IWriteVerdict&) = delete;
    IWriteVerdict(IWriteVerdict&&) = delete;
    IWriteVerdict& operator=(IWriteVerdict&&) = delete;

    virtual void on_written(bool durable) noexcept = 0;

protected:
    IWriteVerdict() = default;
};

}
