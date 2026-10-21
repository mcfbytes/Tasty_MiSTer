// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/transition.h"

namespace mister::ui {

class IHdAction {
public:
    virtual ~IHdAction() = default;

    virtual Transition on_yes() = 0;

protected:
    IHdAction() = default;
    IHdAction(const IHdAction&) = default;
    IHdAction& operator=(const IHdAction&) = default;
};

}
