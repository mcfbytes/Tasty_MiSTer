// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <cstdint>

#include "infra/error.h"
#include "infra/seat.h"
#include "proto/image_sink.h"
#include "proto/session_params.h"
#include "proto/types.h"

namespace mister::proto {

class ImageBracket {
    TASTY_SEAT_EXEMPT(component);

public:
    [[nodiscard]] static Ex<ImageBracket> open(IImageSink& sink, WideIoIndex index,
                                               const SessionParams& params = {});

    ~ImageBracket() {
        if (open_) (void)sink_->end();
    }
    ImageBracket(ImageBracket&& o) noexcept : sink_(o.sink_), open_(o.open_) { o.open_ = false; }
    ImageBracket& operator=(ImageBracket&&) = delete;

    [[nodiscard]] Ex<void> write(std::span<const std::uint8_t> data) { return sink_->write(data); }

    [[nodiscard]] Ex<void> end();

    [[nodiscard]] bool active() const noexcept { return open_; }

private:
    explicit ImageBracket(IImageSink& sink) noexcept : sink_(&sink) {}
    IImageSink* sink_;
    bool open_ = false;
};

}  // namespace mister::proto
