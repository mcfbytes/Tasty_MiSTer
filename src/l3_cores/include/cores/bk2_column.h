// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "cores/movie_frame.h"
#include "cores/types.h"
#include "proto/types.h"

namespace mister::cores {

class Bk2Column {
public:
    enum class Kind : std::uint8_t {
        Unset,
        Reset,
        Power,
        Subframe,
        Tray,
        ResetDelay,
        DiscIndex,
        Pad,
        GroupEnd
    };

    struct PadCell {
        proto::PlayerIndex port;
        JoyBit bit;
        friend constexpr bool operator==(PadCell, PadCell) = default;
    };

    constexpr Bk2Column() noexcept = default;

    [[nodiscard]] static constexpr Bk2Column reset() noexcept { return Bk2Column{Kind::Reset}; }

    [[nodiscard]] static constexpr Bk2Column power() noexcept { return Bk2Column{Kind::Power}; }

    [[nodiscard]] static constexpr Bk2Column subframe() noexcept {
        return Bk2Column{Kind::Subframe};
    }

    [[nodiscard]] static constexpr Bk2Column tray() noexcept { return Bk2Column{Kind::Tray}; }

    [[nodiscard]] static constexpr Bk2Column reset_delay() noexcept {
        return Bk2Column{Kind::ResetDelay};
    }

    [[nodiscard]] static constexpr Bk2Column disc_index() noexcept {
        return Bk2Column{Kind::DiscIndex};
    }

    [[nodiscard]] static constexpr Bk2Column group_end() noexcept {
        return Bk2Column{Kind::GroupEnd};
    }

    [[nodiscard]] static constexpr std::optional<Bk2Column> pad(
        proto::PlayerIndex port,
        JoyBit bit
    ) noexcept {
        if (!MovieFrame::has_port(port) || !bit.fits())
            return std::nullopt;
        return Bk2Column{port, bit};
    }

    [[nodiscard]] constexpr Kind kind() const noexcept { return kind_; }

    [[nodiscard]] constexpr std::optional<PadCell> pad_cell() const noexcept {
        if (kind_ != Kind::Pad)
            return std::nullopt;
        return pad_;
    }

    friend constexpr bool operator==(Bk2Column, Bk2Column) = default;

private:
    constexpr explicit Bk2Column(Kind k) noexcept : kind_(k) {}

    constexpr Bk2Column(proto::PlayerIndex port, JoyBit bit) noexcept
        : kind_(Kind::Pad),
          pad_{port, bit} {}

    Kind kind_ = Kind::Unset;
    PadCell pad_{};
};

}
