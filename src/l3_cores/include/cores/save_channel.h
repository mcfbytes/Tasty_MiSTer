// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cores/types.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "proto/block_geometry.h"
#include "proto/image_source.h"
#include "proto/types.h"
#include "svc/storage_backend.h"

namespace mister::cores {

class ISaveChannel {
public:
    struct Mount {

        SaveRow row;
        proto::FileSize size;
        std::string_view path;

        [[nodiscard]] constexpr proto::SlotIndex slot() const noexcept {
            return proto::SlotIndex{row.v};
        }
    };

    class MountPlan {
        TASTY_SEAT_EXEMPT(component);

    public:
        static constexpr std::size_t kMaxMounts = proto::kAnnounceableSlots;

        void add(Ex<Mount> m) noexcept {
            if (count_ >= kMaxMounts)
                return;
            if (m)
                claimed_ = claimed_.with(m->slot());
            mounts_[count_++] = m;
        }

        [[nodiscard]] std::span<const Ex<Mount>> mounts() const noexcept {
            return std::span<const Ex<Mount>>{mounts_.data(), count_};
        }

        [[nodiscard]] proto::BlockSlotMask claimed() const noexcept { return claimed_; }

    private:
        std::array<Ex<Mount>, kMaxMounts> mounts_{};
        proto::BlockSlotMask claimed_{};
        std::size_t count_ = 0;
    };

    enum class OrderPart : std::uint8_t { RowOnly, RowAndPlan };

    virtual ~ISaveChannel() = default;

    [[nodiscard]] virtual infra::OptRef<const MountPlan> mount_plan() const noexcept = 0;

    [[nodiscard]] virtual svc::IStorageBackend& backend() noexcept = 0;

    [[nodiscard]] virtual std::span<const std::byte> compose_order(
        SaveRow row,
        OrderPart part
    ) noexcept = 0;

    [[nodiscard]] virtual proto::IImageSource& descriptor() noexcept = 0;

protected:
    ISaveChannel() = default;
    ISaveChannel(const ISaveChannel&) = default;
    ISaveChannel& operator=(const ISaveChannel&) = default;
};

}
