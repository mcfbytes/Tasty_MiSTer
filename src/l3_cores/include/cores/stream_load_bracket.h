// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "cores/stream_load.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/image_bracket.h"
#include "proto/image_sink.h"
#include "proto/types.h"
#include "svc/types.h"

namespace mister::cores {

class StreamLoadBracket {
    TASTY_SEAT_RESIDENT(RT);

public:
    [[nodiscard]] static Ex<StreamLoadBracket> open(
        IStreamLoad& core,
        proto::IImageSink& sink,
        IoIndex index,
        StreamLoadKind kind = StreamLoadKind::Stream,
        std::uint32_t length = 0
    );

    StreamLoadBracket(StreamLoadBracket&& o) noexcept;
    StreamLoadBracket& operator=(StreamLoadBracket&&) = delete;
    StreamLoadBracket(const StreamLoadBracket&) = delete;
    StreamLoadBracket& operator=(const StreamLoadBracket&) = delete;
    ~StreamLoadBracket();

    [[nodiscard]] Ex<void> write(std::span<const std::uint8_t> piece);

    [[nodiscard]] Ex<std::span<const std::uint8_t>> write_taken(
        std::span<const std::uint8_t> piece
    );

    [[nodiscard]] Ex<void> close(
        infra::OptRef<IStreamLoad> live,
        proto::FileSize bytes,
        svc::Crc32 crc
    );

    void cut(infra::OptRef<IStreamLoad> live) noexcept;

    [[nodiscard]] StreamLoadKind kind() const noexcept { return kind_; }

private:
    StreamLoadBracket(IoIndex index, StreamLoadKind kind, proto::ImageBracket&& b) noexcept
        : index_(index),
          kind_(kind),
          bracket_(std::move(b)) {}

    void cut_() noexcept;

    IoIndex index_;
    StreamLoadKind kind_;
    std::optional<proto::ImageBracket> bracket_;
};

}
