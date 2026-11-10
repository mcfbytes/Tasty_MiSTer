// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <type_traits>

#include "cores/n64_save_order.h"
#include "cores/n64_save_plan.h"
#include "infra/endian.h"
#include "svc/storage_backend.h"

namespace mister::cores::n64 {

struct SaveAttach {

    struct Planned {
        SaveOrder order{};
        SavePlan plan{};
    };

    SaveOrder order{};
    std::optional<SavePlan> plan{};

    [[nodiscard]] static std::optional<SaveAttach> decode(
        std::span<const std::byte> bytes
    ) noexcept {
        if (bytes.size() == sizeof(SaveOrder)) {
            const SaveOrder order = infra::load_native<SaveOrder>(bytes.first<sizeof(SaveOrder)>());
            if (!SaveOrder::create_fits(order.create, proto::FileSize{order.bytes}))
                return std::nullopt;
            return SaveAttach{order, std::nullopt};
        }
        if (bytes.size() == sizeof(Planned)) {
            const auto both = infra::load_native<Planned>(bytes.first<sizeof(Planned)>());
            if (!SaveOrder::create_fits(both.order.create, proto::FileSize{both.order.bytes}))
                return std::nullopt;
            return SaveAttach{both.order, both.plan};
        }
        return std::nullopt;
    }
};

static_assert(
    std::is_trivially_copyable_v<SaveAttach::Planned>
    && sizeof(SaveAttach::Planned) <= svc::kStorageOrderBytes
);
static_assert(sizeof(SaveAttach::Planned) == sizeof(SaveOrder) + sizeof(SavePlan));

}
