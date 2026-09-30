// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hal/core_signals.h"
#include "hal/spi_sample_source.h"
#include "hal/spi_transport.h"

namespace mister::hal {

class ILinkPort : public ISpiTransport, public ICoreSignals, public ISpiSampleSource {
public:
    ~ILinkPort() override = default;
    ILinkPort() = default;
    ILinkPort(const ILinkPort&) = delete;
    ILinkPort& operator=(const ILinkPort&) = delete;

    [[nodiscard]] virtual bool ready() const = 0;

protected:
    ILinkPort(ILinkPort&&) = default;
    ILinkPort& operator=(ILinkPort&&) = default;
};

}  // namespace mister::hal
