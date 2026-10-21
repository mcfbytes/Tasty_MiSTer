// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "proto/conf_str.h"
#include "ui/option_host.h"
#include "ui/status_view.h"
#include "ui/transition.h"

namespace mister::ui {

class OptionItem;
class OptionsModel;

class IOptionsSurface : public IOptionHost {
public:
    [[nodiscard]] virtual const OptionsModel& walk() const = 0;
    [[nodiscard]] virtual std::string_view heading() const = 0;

    [[nodiscard]] virtual std::uint32_t field(proto::BitField bits) const = 0;

    [[nodiscard]] virtual StatusView live_words() const = 0;

    virtual Transition open_recents(const OptionItem& row) = 0;

    virtual void save_config() = 0;

protected:
    IOptionsSurface() = default;
    IOptionsSurface(const IOptionsSurface&) = default;
    IOptionsSurface& operator=(const IOptionsSurface&) = default;
};

}
