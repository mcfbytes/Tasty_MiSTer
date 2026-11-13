// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/text_entry_result.h"
#include "ui/transition.h"

namespace mister::ui {

class ITextEntryHost {
public:
    virtual ~ITextEntryHost() = default;

    virtual Transition on_entered(const TextEntryResult& result) = 0;

    virtual Transition on_cancel() { return Transition::pop(); }

protected:
    ITextEntryHost() = default;
    ITextEntryHost(const ITextEntryHost&) = default;
    ITextEntryHost& operator=(const ITextEntryHost&) = default;
};

}
