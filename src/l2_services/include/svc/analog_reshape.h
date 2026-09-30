// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::svc {

struct AnalogXy;
struct AxisCal;
struct StickCal;

class IAnalogReshape {
public:
    virtual ~IAnalogReshape() = default;

    virtual AnalogXy reshape(AnalogXy in, const AxisCal& cal,
                             const StickCal& stick) const noexcept = 0;

protected:
    IAnalogReshape() = default;
    IAnalogReshape(const IAnalogReshape&) = default;
    IAnalogReshape& operator=(const IAnalogReshape&) = default;
};

}  // namespace mister::svc
