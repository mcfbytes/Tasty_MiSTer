// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "hal/phys_region.h"
#include "proto/mailbox_act.h"
#include "proto/mailbox_frame.h"
#include "proto/types.h"
#include "proto/uio_opcodes.h"

namespace mister::cores::msu {

inline constexpr std::uint8_t kCdSet = static_cast<std::uint8_t>(proto::UioOp::CdSet);
inline constexpr std::uint8_t kFrameWords = 4;

inline constexpr std::uint16_t kSetEnable = 1;
inline constexpr std::uint16_t kSetTrackSize = 2;
inline constexpr std::uint16_t kSetDataBase = 3;

enum class Request : std::uint16_t { Next = 0x34, Track = 0x35, Seek = 0x36, Reset = 0xFF };

[[nodiscard]] constexpr std::optional<Request> request_of(
    const proto::MailboxFrame& frame
) noexcept {
    const Request request = static_cast<Request>(frame.w[1]);
    switch (request) {
        case Request::Next:
        case Request::Track:
        case Request::Seek:
        case Request::Reset:
            return request;
    }
    return std::nullopt;
}

inline constexpr std::uint32_t kSectorBytes = 1024;
inline constexpr std::uint16_t kAudioIndex = 2;
inline constexpr std::uint8_t kDataIndex = 3;

inline constexpr std::uint32_t kGapAt = 0x01A0'0000u;
inline constexpr std::uint32_t kGapBytes = 0x0080'0000u;

inline constexpr std::uint32_t kStockDataBound = 0x1F20'0000u;
inline constexpr proto::FileSize kDataBound{0x1EA0'0000u};

struct TrackCursor {
    std::uint64_t v = 0;
    using affine_count = proto::FileSize;

    template <std::integral Word>
    [[nodiscard]] constexpr Word file_word() const noexcept {
        return static_cast<Word>(v);
    }

    friend constexpr bool operator==(TrackCursor, TrackCursor) = default;
    friend constexpr auto operator<=>(TrackCursor, TrackCursor) = default;
};

[[nodiscard]] constexpr proto::MailboxWords set_words(std::uint64_t v) noexcept {
    return {
        static_cast<std::uint16_t>(v),
        static_cast<std::uint16_t>((v >> 16)),
        static_cast<std::uint16_t>((v >> 32))
    };
}

[[nodiscard]] constexpr proto::MailboxWords set_enable(bool present) noexcept {
    return set_words((std::uint64_t{present ? 1u : 0u} << 15) | kSetEnable);
}

[[nodiscard]] constexpr proto::MailboxWords set_track_size(std::uint32_t size) noexcept {
    return set_words((std::uint64_t{size} << 16) | kSetTrackSize);
}

[[nodiscard]] constexpr proto::MailboxWords set_data_base(hal::FabricAddr base) noexcept {
    const auto halves = hal::to_phys(base).low_halves();
    return {kSetDataBase, halves[0], halves[1]};
}

[[nodiscard]] constexpr std::uint64_t window_offset(std::uint64_t file_off) noexcept {
    return file_off < kGapAt ? file_off : file_off + kGapBytes;
}

[[nodiscard]] constexpr proto::FileSize data_extent(proto::FileSize size) noexcept {
    return size.holds_offset(kGapAt) ? size + proto::FileSize{kGapBytes} : size;
}

}
