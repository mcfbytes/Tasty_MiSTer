// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <optional>

#include "proto/user_io/poll_frame.h"
#include "proto/user_io/poll_frame_shape.h"

namespace mister::proto::user_io {

template <class D>
concept PollFrameDecoder = requires(const PollFrame& frame) {
    typename D::Request;
    { D::kFrame } -> std::convertible_to<const PollFrameShape&>;
    { D::decode(frame) } noexcept -> std::same_as<std::optional<typename D::Request>>;
};

}
