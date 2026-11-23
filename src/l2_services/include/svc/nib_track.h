// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "infra/span_cut.h"
#include "proto/types.h"

namespace mister::svc {

inline constexpr proto::FileSize kDosSectorBytes{256};
inline constexpr proto::FileSize kDosTrackBytes{4096};
inline constexpr proto::FileSize kNibSectorBytes{416};
inline constexpr proto::FileSize kNibTrackBytes{6656};

inline constexpr proto::FileSize kDosDiskBytes{143'360};
inline constexpr proto::FileSize kNibImageBytes{232'960};

using DosSectorBytes = std::array<std::byte, kDosSectorBytes.file_word<std::size_t>()>;

struct NibTrack {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr std::optional<NibTrack> holding(proto::FileOffset at) noexcept {
        if (!kNibImageBytes.holds(at))
            return std::nullopt;
        return NibTrack{
            static_cast<std::uint8_t>((at - proto::kFileStart).whole_records(kNibTrackBytes))
        };
    }

    [[nodiscard]] constexpr std::optional<NibTrack> next() const noexcept {
        return holding(image_start() + kNibTrackBytes);
    }

    [[nodiscard]] constexpr proto::FileOffset image_start() const noexcept {
        return proto::kFileStart + kNibTrackBytes * std::uint64_t{v};
    }

    [[nodiscard]] constexpr proto::FileOffset source_start() const noexcept {
        return proto::kFileStart + kDosTrackBytes * std::uint64_t{v};
    }

    [[nodiscard]] constexpr std::byte address() const noexcept { return std::byte{v}; }

    template <class B>
    [[nodiscard]] constexpr std::span<B> nibbles_in(std::span<B> image) const noexcept {
        return infra::affine::first(infra::affine::from(image, image_start()), kNibTrackBytes);
    }

    template <class B>
    [[nodiscard]] constexpr std::span<B> sectors_in(std::span<B> disk) const noexcept {
        return infra::affine::first(infra::affine::from(disk, source_start()), kDosTrackBytes);
    }

    friend constexpr bool operator==(NibTrack, NibTrack) = default;
};

}
