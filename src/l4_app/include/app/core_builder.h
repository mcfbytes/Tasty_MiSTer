// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

#include "app/bay_releases.h"
#include "app/built_core.h"
#include "app/core_terms.h"
#include "app/doorbell_lines.h"
#include "app/park_receipt.h"
#include "cores/core_manifest.h"
#include "cores/core_window_grant.h"
#include "hal/doorbell_policy.h"
#include "hal/doorbell_source.h"
#include "hal/uio_doorbell_source.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "proto/irq_binding.h"
#include "proto/link_op.h"

namespace mister::cores {
struct CoreProfile;
}

namespace mister::app {

struct BayCounts {
    std::uint32_t builds = 0;
    std::uint32_t bay_full = 0;
    std::uint32_t frees = 0;
    std::uint32_t window_refusals = 0;
};
static_assert(std::is_trivially_copyable_v<BayCounts>);
using BayCountsCell = xthread::Telemetry<BayCounts, SeatTag::Main>;

class CoreBuilder {
    TASTY_SEAT_RESIDENT(Main);

public:
    struct Ask {
        std::string_view name;
        cores::CoreManifest manifest{};
        std::span<const proto::IrqBinding> irqs{};
    };

    CoreBuilder(CoreBay& bay, const BayReleasesCell& releases, const CoreTerms& terms) noexcept
        : CoreBuilder(bay, releases, terms, infra::OptRef<hal::IDoorbellSource>{}) {}

    CoreBuilder(CoreBay& bay, const BayReleasesCell& releases, const CoreTerms& terms,
                hal::IDoorbellSource& src) noexcept
        : CoreBuilder(bay, releases, terms, infra::OptRef<hal::IDoorbellSource>{src}) {}

    CoreBuilder(CoreBay& bay, const BayReleasesCell& releases, const CoreTerms& terms,
                infra::OptRef<hal::IDoorbellSource> src) noexcept;
    CoreBuilder(const CoreBuilder&) = delete;
    CoreBuilder& operator=(const CoreBuilder&) = delete;
    ~CoreBuilder();

    [[nodiscard]] proto::LinkOp::AdoptCore build(const Ask& ask);

    void forget(const proto::LinkOp::AdoptCore& op) noexcept;

    void reap() noexcept;

    void drop_windows(const ParkReceipt& receipt) noexcept;

    [[nodiscard]] cores::CoreWindowGrant grant_windows(BuiltCore& into,
                                                       const cores::CoreProfile& profile) noexcept;

    static void open_doorbells(hal::IDoorbellSource& src, const hal::DoorbellPolicy& policy,
                               std::span<const proto::IrqBinding> irqs,
                               DoorbellLines& into) noexcept;

    [[nodiscard]] std::uint32_t builds() const noexcept { return builds_; }

    [[nodiscard]] std::uint32_t bay_full() const noexcept { return bay_full_; }
    [[nodiscard]] std::uint32_t frees() const noexcept { return frees_; }

    [[nodiscard]] std::uint32_t window_refusals() const noexcept { return window_refusals_; }

    [[nodiscard]] std::uint32_t leased() const noexcept;
    [[nodiscard]] const BayCountsCell& counts_cell() const noexcept { return counts_cell_; }

private:
    void free_(std::uint8_t slot) noexcept;
    void publish_counts_() noexcept;

    void open_doorbells_(BuiltCore& into, std::span<const proto::IrqBinding> irqs) noexcept;

    CoreBay& bay_;
    const BayReleasesCell& releases_;
    const CoreTerms& terms_;
    hal::UioDoorbellSource uio_doorbells_{};
    hal::IDoorbellSource& doorbells_;
    BayReleasesCell::Reader released_{};
    BayReleases levels_{};
    std::array<CoreBay::Lease, kCoreBaySlots> held_{};
    std::uint16_t gen_ = 0;
    std::uint32_t builds_ = 0;
    std::uint32_t bay_full_ = 0;
    std::uint32_t frees_ = 0;
    std::uint32_t window_refusals_ = 0;
    BayCountsCell counts_cell_{};
};

}  // namespace mister::app
