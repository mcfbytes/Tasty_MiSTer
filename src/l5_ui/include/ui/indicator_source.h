// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/indicator_canvas.h"
#include "ui/source_revision.h"

namespace mister::ui {

class IIndicatorSource {
public:
    virtual ~IIndicatorSource() = default;
    [[nodiscard]] virtual SourceRevision revision() const noexcept = 0;
    [[nodiscard]] virtual IndicatorCanvas draw() const = 0;

protected:
    IIndicatorSource() = default;
    IIndicatorSource(const IIndicatorSource&) = default;
    IIndicatorSource& operator=(const IIndicatorSource&) = default;
};

}
