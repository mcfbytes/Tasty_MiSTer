// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "cores/stream_load.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "proto/image_bracket.h"
#include "proto/image_sink.h"
#include "proto/types.h"

namespace mister::cores {

class StreamLoadBracket {
    TASTY_SEAT_RESIDENT(RT);

public:
    [[nodiscard]] static Ex<StreamLoadBracket> open(IStreamLoad& core, proto::IImageSink& sink,
                                                    IoIndex index, std::uint32_t length = 0);

    StreamLoadBracket(StreamLoadBracket&& o) noexcept;
    StreamLoadBracket& operator=(StreamLoadBracket&&) = delete;
    StreamLoadBracket(const StreamLoadBracket&) = delete;
    StreamLoadBracket& operator=(const StreamLoadBracket&) = delete;
    ~StreamLoadBracket();

    [[nodiscard]] Ex<void> write(std::span<const std::uint8_t> piece);

    [[nodiscard]] Ex<void> close(std::uint64_t bytes, std::uint32_t crc);

private:
    StreamLoadBracket(IStreamLoad& core, IoIndex index, proto::ImageBracket&& b) noexcept
        : core_(&core), index_(index), bracket_(std::move(b)) {}
    void cut_() noexcept;

    IStreamLoad* core_;
    IoIndex index_;
    std::optional<proto::ImageBracket> bracket_;
};

}  // namespace mister::cores
