// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/connect_port.h"

#include "infra/message_sum.h"

namespace mister::ui {

bool ConnectPort::send(const svc::ConnectRequest& request) noexcept {
    if (!sender_) {
        refusals_.note_unwired();
        return false;
    }
    if (!sender_.send(request)) {
        refusals_.note_full();
        return false;
    }
    return true;
}

bool ConnectPort::pump(ConnectDemand wanted) noexcept {
    bool moved = false;
    if (auto v = viewer_.take_wifi()) {
        views_.wifi = *v;
        moved = true;
    }
    if (auto v = viewer_.take_scan()) {
        views_.scan = *v;
        moved = true;
    }
    if (auto v = viewer_.take_saved()) {
        views_.saved = *v;
        moved = true;
    }
    if (auto v = viewer_.take_bt()) {
        views_.bt = *v;
        moved = true;
    }
    if (auto v = viewer_.take_devices()) {
        views_.devices = *v;
        moved = true;
    }
    if (auto v = viewer_.take_prompt()) {
        views_.prompt = *v;
        moved = true;
    }
    if (!sender_)
        return moved;

    if (!accepted_.same_wifi(wanted)
        && sender_.send(
            infra::make<svc::ConnectRequest>(
                svc::ConnectRequest::WifiPage{.open = wanted.wifi_open()}
            )
        )) {
        accepted_ = accepted_.adopting_wifi(wanted);
    }
    if (!accepted_.same_bluetooth(wanted)
        && sender_.send(
            infra::make<svc::ConnectRequest>(svc::ConnectRequest::BtPage{
                .mode = wanted.bt_mode(),
                .auto_first = wanted.pair_start() == PairStart::AutoFirst
            })
        )) {
        accepted_ = accepted_.adopting_bluetooth(wanted);
    }
    return moved;
}

}
