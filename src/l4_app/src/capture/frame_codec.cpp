// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/capture/frame_codec.h"

#include "infra/prelude.h"

namespace mister::app::capture {

Ex<void> IFrameCodec::begin(PictureSize size) noexcept {
    end();
    if (size.empty())
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), 0});
    return begin_(size);
}

}
