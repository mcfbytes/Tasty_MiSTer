// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_codec.h"

namespace mister::app {

Ex<void> IFrameCodec::begin(PictureSize size) noexcept {
    end();
    if (size.empty())
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), 0});
    return begin_(size);
}

}
