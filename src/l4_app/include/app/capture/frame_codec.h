// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "app/capture/frame_budget.h"
#include "app/picture_size.h"
#include "app/capture/rec_options.h"
#include "app/rgb_rows.h"
#include "infra/error.h"
#include "infra/prelude.h"

namespace mister::app::capture {

class IFrameCodec {
public:
    virtual ~IFrameCodec() = default;

    struct Search {
        std::uint8_t radius = 0;
        std::uint32_t cut = 0;
    };

    struct Encoded {
        std::span<const std::byte> payload{};
        Search search{};
    };

    [[nodiscard]] virtual Search restart(const RecOptions& opt) noexcept = 0;

    [[nodiscard]] Ex<void> begin(PictureSize size) noexcept;
    virtual void end() noexcept = 0;

    [[nodiscard]] virtual PictureSize size() const noexcept = 0;
    [[nodiscard]] virtual std::size_t max_payload() const noexcept = 0;
    [[nodiscard]] virtual Encoded encode(
        RgbRows rgb,
        bool key,
        std::span<std::byte> out,
        const FrameBudget& budget
    ) noexcept = 0;
    [[nodiscard]] virtual std::span<const std::byte> rekey(std::span<std::byte> out) noexcept = 0;

    [[nodiscard]] virtual std::span<const std::byte> dup() noexcept = 0;

private:
    [[nodiscard]] virtual Ex<void> begin_(PictureSize size) noexcept = 0;
};

}
