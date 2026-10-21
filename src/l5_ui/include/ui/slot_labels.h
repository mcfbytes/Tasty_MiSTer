// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::ui {

class ISlotLabels {
public:
    virtual ~ISlotLabels() = default;

    virtual bool has_label(std::uint32_t v) const noexcept = 0;

protected:
    ISlotLabels() = default;
    ISlotLabels(const ISlotLabels&) = default;
    ISlotLabels& operator=(const ISlotLabels&) = default;
};

}
