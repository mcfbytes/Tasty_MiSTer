// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/off_link_window.h"

#include "infra/prelude.h"
#include "proto/aux_value.h"

namespace mister::cores {

Ex<OffLinkWindow> OffLinkWindow::open(
    IStreamLoad& core,
    const StreamAsk& ask,
    StreamLoadKind kind,
    proto::AuxValue length
) {
    auto params = core.stream_opening(ask);
    if (!params)
        return std::unexpected(params.error());
    if (!length.empty())
        params->aux = length;
    return OffLinkWindow(
        ask.index,
        kind,
        proto::link::FioOpen::of(proto::WideIoIndex::of(ask.index), *params)
    );
}

void OffLinkWindow::close(
    infra::OptRef<IStreamLoad> live,
    proto::FileSize bytes,
    svc::Crc32 crc
) noexcept {
    TASTY_SEAT_BODY(OffLinkWindow);
    if (live) {
        live->stream_closed(StreamLoadEnd{.index = index_, .bytes = bytes, .crc = crc, .ok = true});
    }
}

void OffLinkWindow::cut(infra::OptRef<IStreamLoad> live) noexcept {
    TASTY_SEAT_BODY(OffLinkWindow);
    if (live)
        live->stream_closed(StreamLoadEnd{.index = index_});
}

}
