// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/conf_str.h"
#include "ui/transition.h"

namespace mister::ui {

class OptionItem;

class IOptionHost {
public:
    virtual ~IOptionHost() = default;

    virtual void refuse() = 0;

    virtual void write_status(proto::BitField bits, std::uint32_t value) = 0;

    virtual void toggle(proto::BitField bits, bool ex) = 0;

    virtual Transition open_browser(const OptionItem& row) = 0;

protected:
    IOptionHost() = default;
    IOptionHost(const IOptionHost&) = default;
    IOptionHost& operator=(const IOptionHost&) = default;
};

}
