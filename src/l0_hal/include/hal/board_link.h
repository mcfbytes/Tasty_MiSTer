// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hal/core_signals.h"
#include "hal/pin_sample_source.h"
#include "hal/spi_transport.h"

namespace mister::hal {

class IBoardLink : public ISpiTransport, public ICoreSignals, public IPinSampleSource {
public:
    ~IBoardLink() override = default;
    IBoardLink() = default;
    IBoardLink(const IBoardLink&) = delete;
    IBoardLink& operator=(const IBoardLink&) = delete;

    [[nodiscard]] virtual bool ready() const = 0;

protected:
    IBoardLink(IBoardLink&&) = default;
    IBoardLink& operator=(IBoardLink&&) = default;
};

}
