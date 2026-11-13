// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "app/avi_frame_index.h"
#include "app/segment_index.h"
#include "infra/decimal_text.h"

namespace mister::app {

class ChunkPlace {
public:
    [[nodiscard]] static constexpr ChunkPlace none() noexcept { return ChunkPlace{}; }

    [[nodiscard]] static constexpr ChunkPlace written(
        SegmentIndex segment,
        AviFrameIndex frame
    ) noexcept {
        ChunkPlace place{};
        place.segment_ = segment;
        place.state_ = State::Written;
        place.frame_ = frame;
        return place;
    }

    [[nodiscard]] static constexpr ChunkPlace held(SegmentIndex segment) noexcept {
        ChunkPlace place{};
        place.segment_ = segment;
        place.state_ = State::Held;
        return place;
    }

    [[nodiscard]] constexpr bool shows_hash() const noexcept {
        switch (state_) {
            case State::Held:
                return false;
            case State::None:
            case State::Written:
                break;
        }
        return true;
    }

    [[nodiscard]] constexpr infra::DecimalText segment_column() const noexcept {
        switch (state_) {
            case State::None:
                return no_column();
            case State::Held:
            case State::Written:
                break;
        }
        return segment_.digits();
    }

    [[nodiscard]] constexpr infra::DecimalText frame_column() const noexcept {
        switch (state_) {
            case State::None:
            case State::Held:
                return no_column();
            case State::Written:
                break;
        }
        return frame_.digits();
    }

    friend constexpr bool operator==(ChunkPlace, ChunkPlace) = default;

private:
    [[nodiscard]] static constexpr infra::DecimalText no_column() noexcept {
        return infra::DecimalText{-1};
    }

    enum class State : std::uint8_t { None, Held, Written };

    SegmentIndex segment_{};
    State state_ = State::None;
    std::array<std::byte, 1> pad_{};
    AviFrameIndex frame_{};
};

static_assert(sizeof(ChunkPlace) == 8);

}
