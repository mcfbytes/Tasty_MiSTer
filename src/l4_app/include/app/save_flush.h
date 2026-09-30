// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::app {

class ISaveFlush {
public:
    virtual ~ISaveFlush() = default;

    [[nodiscard]] virtual bool save_upload_counted() noexcept = 0;

protected:
    ISaveFlush() = default;
    ISaveFlush(const ISaveFlush&) = default;
    ISaveFlush& operator=(const ISaveFlush&) = default;
};

}  // namespace mister::app
