// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::hal {

class IHdmiInterrupt {
public:
    virtual ~IHdmiInterrupt() = default;

    virtual bool hdmi_int_asserted() const = 0;

protected:
    IHdmiInterrupt() = default;
    IHdmiInterrupt(const IHdmiInterrupt&) = default;
    IHdmiInterrupt& operator=(const IHdmiInterrupt&) = default;
};

}  // namespace mister::hal
