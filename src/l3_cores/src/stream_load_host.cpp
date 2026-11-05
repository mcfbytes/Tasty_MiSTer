// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/stream_load_host.h"

namespace mister::cores {

Ex<proto::SessionParams> StreamLoadHost::stream_opening(IoIndex) {
    TASTY_SEAT_BODY(StreamLoadHost);
    tx_.opened();
    proto::SessionParams params{};
    params.ext = tx_.ext();
    return params;
}

void StreamLoadHost::stream_closed(const StreamLoadEnd& end) noexcept {
    TASTY_SEAT_BODY(StreamLoadHost);
    last_index_ = static_cast<std::uint16_t>(end.index.v);
    ++transfers_;
    chunks_ += static_cast<std::uint32_t>((end.bytes.v + kChunkBytes - 1) / kChunkBytes);
    tx_.closed(end);
}

Ex<void> StreamLoadHost::mount(IoIndex slot, const MountedPath&) {

    ++mount_refusals_;
    (void)slot;
    return unimplemented(ERR_SITE());
}

}
