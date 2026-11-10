// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/image_bracket.h"

namespace mister::proto {

Ex<ImageBracket> ImageBracket::open(
    IImageSink& sink,
    WideIoIndex index,
    const SessionParams& params
) {
    ImageBracket b(sink);
    if (auto r = sink.begin(index, params); !r) {
        return std::unexpected(r.error());
    }
    b.open_ = true;
    return b;
}

Ex<void> ImageBracket::end() {
    if (!open_)
        return {};
    open_ = false;
    return sink_->end();
}

}
