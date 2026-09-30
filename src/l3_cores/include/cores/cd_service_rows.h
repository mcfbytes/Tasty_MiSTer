// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::cores {

class ICdServiceRows {
public:
    virtual ~ICdServiceRows() = default;

    virtual void service_command_edge() noexcept = 0;
    virtual void service_tick() noexcept = 0;

protected:
    ICdServiceRows() = default;
    ICdServiceRows(const ICdServiceRows&) = default;
    ICdServiceRows& operator=(const ICdServiceRows&) = default;
};

}  // namespace mister::cores
