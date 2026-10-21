// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "app/page_description.h"
#include "ui/transition.h"
#include "ui/types.h"

namespace mister::ui {

class IHdRow {
public:
    virtual ~IHdRow() = default;

    [[nodiscard]] virtual bool shown() const { return true; }

    [[nodiscard]] virtual std::optional<RowKey> key() const { return std::nullopt; }

    [[nodiscard]] virtual app::PageDescription::Row describe() const = 0;
    virtual Transition select() = 0;

    virtual Transition step(int delta) {
        (void)delta;
        return Transition{};
    }

protected:
    IHdRow() = default;
    IHdRow(const IHdRow&) = default;
    IHdRow& operator=(const IHdRow&) = default;
};

}
