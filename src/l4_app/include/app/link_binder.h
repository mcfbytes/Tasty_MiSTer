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
#include "app/doorbell_cell.h"
#include "app/doorbell_lines.h"
#include "app/link_rows.h"
#include "app/progress_image_sink.h"
#include "infra/log_lane.h"
#include "reactor/core_notifiers.h"
#include "reactor/executive.h"
#include "hal/fpga_aperture.h"
#include "hal/fpga_memory.h"
#include "hal/phys_region.h"
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
    };

    LinkBinder(hal::ISpiTransport& link, const Wiring& w) noexcept;

    void service_on_caller(std::int64_t now_ns) noexcept { exec_.service_on_caller(now_ns); }

    [[nodiscard]] proto::IImageSink& grant_bulk() noexcept;

    [[nodiscard]] proto::SpiFioQueue& fio_queue() noexcept { return fio_queue_; }

    [[nodiscard]] ProgressImageSink& file_progress() noexcept { return progress_sink_; }

    [[nodiscard]] bool adopt_fed_ring(hal::FpgaMemory&& ring) noexcept;

    void release_feeder() noexcept;

    void set_pcm_feeder(PcmRingFeeder* feeder) noexcept { feeder_ = feeder; }

    [[nodiscard]] Ex<void> bind_census(std::span<const reactor::LinkDecoderDecl> rows,
                                       cores::Core* owner);

    [[nodiscard]] Ex<void> bind_session(std::span<const reactor::LinkDecoderDecl> rows,
                                        cores::Core* owner, std::span<const proto::IrqBinding> irqs,
                                        std::uint32_t declared, const DoorbellLines& lines);

    [[nodiscard]] Ex<void> unbind_session();

    [[nodiscard]] bool bridge_windows_live() const noexcept;

    void retry_release() noexcept { notifiers_.retry_release(); }

    [[nodiscard]] std::uint32_t doorbells_declared() const noexcept { return doorbells_declared_; }
    [[nodiscard]] std::size_t doorbells_bound() const noexcept { return notifiers_.size(); }

    [[nodiscard]] std::uint32_t doorbell_fallbacks() const noexcept { return fallbacks_; }

    [[nodiscard]] std::uint32_t fed_refusals() const noexcept { return fed_refusals_; }

    [[nodiscard]] DoorbellStats doorbell_stats() const noexcept;

private:
    void bind_doorbells_(std::span<const proto::IrqBinding> irqs, std::uint32_t declared,
                         const DoorbellLines& lines) noexcept;

    void bind_one_doorbell(const proto::IrqBinding& b, const DoorbellLines::Line& line) noexcept;
    void fall_back(const proto::IrqBinding& b, Errc why) noexcept;

    proto::SpiFioQueue fio_queue_;
    proto::SpiImageSink spi_sink_;
    ProgressImageSink progress_sink_{spi_sink_};
    PcmRingFeeder* feeder_ = nullptr;
    bool feeder_granted_ = false;
    std::uint32_t fed_refusals_ = 0;
    reactor::CauseSet bound_classes_;
    reactor::Executive& exec_;
    reactor::CoreState& state_;
    reactor::CoreNotifiers notifiers_;
    xthread::LogLane& log_lane_;
    std::uint32_t doorbells_declared_ = 0;
    std::uint32_t fallbacks_ = 0;
};

}  // namespace mister::app
