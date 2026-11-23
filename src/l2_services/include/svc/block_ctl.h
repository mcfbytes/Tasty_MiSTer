// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/storage_completion.h"
#include "proto/storage_request.h"
#include "svc/disk_format_row.h"
#include "svc/types.h"

namespace mister::svc {

class IStorageBackend;
struct BlankSaveSpec;

inline constexpr std::size_t kStorageOrderBytes = 2304;

struct BlockCtl {
    TASTY_SEAT_MEDIATOR(Any, Any);

    class StorageOrder {
        TASTY_SEAT_EXEMPT(component);

    public:
        [[nodiscard]] bool set(
            std::span<const std::byte> src,
            std::span<const DiskFormatRow> formats = {}
        ) noexcept {
            if (src.size() > kStorageOrderBytes)
                return false;
            len_ = src.size();
            formats_ = formats;
            if (!src.empty())
                std::memcpy(bytes_, src.data(), src.size());
            return true;
        }

        void clear() noexcept {
            len_ = 0;
            formats_ = {};
        }

        [[nodiscard]] std::span<const std::byte> view() const noexcept { return {bytes_, len_}; }

        [[nodiscard]] std::span<const DiskFormatRow> formats() const noexcept { return formats_; }

    private:
        std::size_t len_ = 0;
        std::span<const DiskFormatRow> formats_{};
        alignas(8) std::byte bytes_[kStorageOrderBytes]{};
    };

    proto::StorageRequest ask{};
    proto::StorageCompletion answer{};
    SlotBindGen gen{};

    infra::OptRef<IStorageBackend> backend{};

    StorageOrder order{};

    infra::OptRef<const BlankSaveSpec> blank{};

    infra::OptRef<IStorageBackend> stage{};
};

static_assert(
    std::is_trivially_copyable_v<BlockCtl>
    && sizeof(BlockCtl) <= 104u
            + kStorageOrderBytes
            + sizeof(infra::OptRef<const BlankSaveSpec>)
            + sizeof(std::span<const DiskFormatRow>)
);

}
