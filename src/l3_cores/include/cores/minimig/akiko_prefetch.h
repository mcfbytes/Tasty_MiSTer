// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "cores/minimig/cd_disc.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "proto/types.h"

namespace mister::cores::minimig::akiko {

class AkikoPrefetch {
    TASTY_SEAT_RESIDENT(Pcm);

public:
    static constexpr proto::BlockCount kSectors{128};
    static constexpr std::size_t kWindows = 4;

    enum class Got : std::uint8_t { Sector, FillFailed, Unreadable };

    AkikoPrefetch() : windows_(std::make_unique<Windows>()) {}

    void invalidate() noexcept;
    [[nodiscard]] Got get(const CdDisc& disc, proto::Lba lba, RawFrame out) noexcept;

    [[nodiscard]] infra::Tally hits() const noexcept { return hits_; }

    [[nodiscard]] infra::Tally misses() const noexcept { return misses_; }

private:
    static constexpr std::size_t kSlots = kSectors / proto::BlockCount{1};

    struct WindowIndex {
        std::uint8_t v = 0;
        friend constexpr bool operator==(WindowIndex, WindowIndex) = default;
    };

    struct WindowSlot {
        std::uint8_t v = 0;

        [[nodiscard]] static constexpr WindowSlot of(proto::BlockCount offset) noexcept {
            return WindowSlot{static_cast<std::uint8_t>(offset.v)};
        }

        friend constexpr bool operator==(WindowSlot, WindowSlot) = default;
    };

    struct UseStamp {
        std::uint32_t v = 0;

        [[nodiscard]] constexpr UseStamp next() const noexcept { return UseStamp{v + 1}; }

        friend constexpr bool operator==(UseStamp, UseStamp) = default;
        friend constexpr auto operator<=>(UseStamp, UseStamp) = default;
    };

    struct Window {

        struct Slot {
            bool valid = false;
            std::array<std::byte, svc::disc::kRawFrameBytes> frame{};
        };

        std::optional<proto::Lba> base{};
        UseStamp stamp{};
        infra::IndexedArray<Slot, kSlots, WindowSlot> slots{};

        [[nodiscard]] constexpr bool holds(proto::Lba lba) const noexcept {
            return base && lba >= *base && lba < *base + kSectors;
        }

        [[nodiscard]] constexpr Slot& slot(proto::Lba lba) noexcept {
            return slots[WindowSlot::of(lba - *base)];
        }
    };

    using Windows = infra::IndexedArray<Window, kWindows, WindowIndex>;

    [[nodiscard]] std::optional<WindowIndex> find_(proto::Lba lba) const noexcept;
    [[nodiscard]] WindowIndex evict_() const noexcept;

    [[nodiscard]] static bool fill_(const CdDisc& disc, Window& w, proto::Lba base) noexcept;

    std::unique_ptr<Windows> windows_;
    UseStamp lru_{};
    infra::Tally hits_{};
    infra::Tally misses_{};
};

}
