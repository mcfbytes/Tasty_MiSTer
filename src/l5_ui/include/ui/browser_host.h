// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

#include "ui/page.h"

namespace mister::ui {

struct BrowserEntry;

class IBrowserHost {
public:
    virtual ~IBrowserHost() = default;

    virtual Transition on_picked(const BrowserEntry& e, std::string_view path) = 0;
    virtual Transition on_cancel() = 0;

    virtual Transition on_eject() { return Transition{}; }

    virtual Transition on_clear() { return Transition{}; }

    virtual std::string display_alias(std::string_view, bool) const { return {}; }

protected:
    IBrowserHost() = default;
    IBrowserHost(const IBrowserHost&) = default;
    IBrowserHost& operator=(const IBrowserHost&) = default;
};

}
