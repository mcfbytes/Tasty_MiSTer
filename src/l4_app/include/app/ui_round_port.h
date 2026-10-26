// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/ui_round_edge.h"

namespace mister::proto {
struct LinkEvent;
}

namespace mister::app {

class IUiRoundPort {
public:
    virtual ~IUiRoundPort() = default;

    virtual void boot_witness() = 0;
    [[nodiscard]] virtual UiRoundEdge round(bool link_work) = 0;

    virtual void publish_edges(const UiRoundEdge& edge) = 0;
    virtual void forget_cells() noexcept = 0;
    virtual bool take_link(const proto::LinkEvent& ev) noexcept = 0;

    virtual void settle() noexcept = 0;

protected:
    IUiRoundPort() = default;
    IUiRoundPort(const IUiRoundPort&) = default;
    IUiRoundPort& operator=(const IUiRoundPort&) = default;
};

}
