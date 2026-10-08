// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include "infra/error.h"
#include "infra/seat.h"
#include "app/core_window.h"
#include "app/doorbell_cell.h"
#include "app/link_rows.h"
#include "app/progress_image_sink.h"
#include "cores/core_window_grant.h"
#include "infra/log_lane.h"
#include "reactor/core_notifiers.h"
#include "hal/doorbell_policy.h"
#include "hal/doorbell_source.h"
#include "reactor/executive.h"
#include "hal/fpga_aperture.h"
#include "hal/fpga_memory.h"
#include "hal/phys_region.h"
#include "hal/uio_doorbell_source.h"
#include "proto/irq_binding.h"
#include "proto/image_sink.h"
#include "proto/spi_fio_queue.h"
#include "proto/spi_image_sink.h"

namespace mister::cores {
class Core;
struct CoreProfile;
}  // namespace mister::cores

namespace mister::app {

class PcmRingFeeder;

class LinkBinder {
    TASTY_SEAT_RESIDENT(RT);

public:
    struct Wiring {
        reactor::Executive& exec;
        reactor::CoreState& state;
        xthread::LogLane& log_lane;
        hal::DoorbellPolicy doorbells{};
        hal::FpgaAperture fpga_mem{};
        hal::PhysRegion lw_window{};
        os::UioLineSpace doorbell_nodes{};
    };

    LinkBinder(hal::ISpiTransport& link, const Wiring& w) noexcept
        : LinkBinder(link, uio_doorbells_, w) {}

    LinkBinder(hal::ISpiTransport& link, hal::IDoorbellSource& src, const Wiring& w) noexcept;

    void service_on_caller(std::int64_t now_ns) noexcept { exec_.service_on_caller(now_ns); }

    [[nodiscard]] proto::IImageSink& grant_bulk() noexcept;

    [[nodiscard]] proto::SpiFioQueue& fio_queue() noexcept { return fio_queue_; }

    [[nodiscard]] ProgressImageSink& file_progress() noexcept { return progress_sink_; }

    [[nodiscard]] cores::CoreWindowGrant grant_windows(const cores::CoreProfile& profile) noexcept;

    void release_windows() noexcept;

    void set_pcm_feeder(PcmRingFeeder* feeder) noexcept { feeder_ = feeder; }

    [[nodiscard]] Ex<void> bind_census(std::span<const reactor::LinkDecoderDecl> rows,
                                       cores::Core* owner);

    [[nodiscard]] Ex<void> bind_session(std::span<const reactor::LinkDecoderDecl> rows,
                                        cores::Core* owner, std::span<const proto::IrqBinding> irqs,
                                        std::uint32_t declared);

    [[nodiscard]] Ex<void> unbind_session();

    [[nodiscard]] bool bridge_windows_live() const noexcept;

    [[nodiscard]] std::uint32_t doorbells_declared() const noexcept { return doorbells_declared_; }
    [[nodiscard]] std::size_t doorbells_bound() const noexcept { return notifiers_.size(); }

    [[nodiscard]] std::uint32_t doorbell_fallbacks() const noexcept { return fallbacks_; }

    [[nodiscard]] std::uint32_t window_refusals() const noexcept { return window_refusals_; }

    [[nodiscard]] DoorbellStats doorbell_stats() const noexcept;

    void emplace_window_for_test(std::size_t slot, CoreWindow w) noexcept {
        if (slot < windows_.size()) windows_[slot].emplace(std::move(w));
    }

    [[nodiscard]] bool grant_fed_ring_for_test(hal::FpgaMemory ring) noexcept {
        return grant_fed_(std::move(ring));
    }

private:
    [[nodiscard]] bool grant_fed_(hal::FpgaMemory ring) noexcept;

    void bind_doorbells_(std::span<const proto::IrqBinding> irqs, std::uint32_t declared) noexcept;

    void bind_one_doorbell(const proto::IrqBinding& b) noexcept;
    void fall_back(const proto::IrqBinding& b, Errc why) noexcept;

    proto::SpiFioQueue fio_queue_;
    proto::SpiImageSink spi_sink_;
    ProgressImageSink progress_sink_{spi_sink_};
    hal::FpgaAperture aperture_{};
    std::array<std::optional<CoreWindow>, cores::kMaxCoreWindows> windows_{};
    PcmRingFeeder* feeder_ = nullptr;
    bool feeder_granted_ = false;
    std::uint32_t window_refusals_ = 0;
    hal::UioDoorbellSource uio_doorbells_{};
    hal::IDoorbellSource& doorbells_;
    hal::DoorbellPolicy policy_{};
    reactor::CauseSet bound_classes_;
    reactor::Executive& exec_;
    reactor::CoreState& state_;
    reactor::CoreNotifiers notifiers_;
    xthread::LogLane& log_lane_;
    std::uint32_t doorbells_declared_ = 0;
    std::uint32_t fallbacks_ = 0;
};

}  // namespace mister::app
