// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::cores {

class IPcmWireRows {
public:
    virtual ~IPcmWireRows() = default;

    virtual void service_pcm_tick() noexcept = 0;

protected:
    IPcmWireRows() = default;
    IPcmWireRows(const IPcmWireRows&) = default;
    IPcmWireRows& operator=(const IPcmWireRows&) = default;
};

}  // namespace mister::cores
