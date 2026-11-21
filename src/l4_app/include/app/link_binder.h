// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "app/progress_image_sink.h"
#include "infra/prelude.h"
#include "reactor/executive.h"
#include "hal/fpga_memory.h"
#include "proto/image_sink.h"
#include "proto/link/spi_fio_queue.h"
#include "proto/link/spi_image_sink.h"

namespace mister::cores {
class Core;
struct CoreProfile;
}

namespace mister::app {

namespace pcm {
class PcmRingFeeder;
}

class LinkBinder {
    TASTY_SEAT_RESIDENT(RT);

public:
    struct Wiring {
        reactor::Executive& exec;
        reactor::CoreState& state;

        infra::OptRef<pcm::PcmRingFeeder> feeder{};

        infra::OptRef<proto::IFioTenant> tenant{};
    };

    LinkBinder(hal::ISpiTransport& link, const Wiring& w) noexcept;

    void service_on_caller() noexcept { exec_.service_on_caller(); }

    [[nodiscard]] proto::IImageSink& grant_bulk() noexcept;

    [[nodiscard]] proto::link::SpiFioQueue& fio_queue() noexcept { return fio_queue_; }

    [[nodiscard]] ProgressImageSink& file_progress() noexcept { return progress_sink_; }

    [[nodiscard]] bool adopt_fed_ring(hal::FpgaMemory&& ring) noexcept;

    void release_feeder() noexcept;

    [[nodiscard]] Ex<void> bind_census(
        std::span<const reactor::LinkDecoderDecl> rows,
        infra::OptRef<cores::Core> owner
    );

    [[nodiscard]] Ex<void> bind_session(
        std::span<const reactor::LinkDecoderDecl> rows,
        infra::OptRef<cores::Core> owner
    );

    [[nodiscard]] Ex<void> unbind_session();

    [[nodiscard]] bool bridge_windows_live() const noexcept;

    [[nodiscard]] std::uint32_t fed_refusals() const noexcept { return fed_refusals_; }

private:
    proto::link::SpiFioQueue fio_queue_;
    proto::link::SpiImageSink spi_sink_;
    ProgressImageSink progress_sink_{spi_sink_};
    infra::OptRef<pcm::PcmRingFeeder> feeder_{};
    bool feeder_granted_ = false;
    std::uint32_t fed_refusals_ = 0;
    reactor::Executive& exec_;
    reactor::CoreState& state_;
};

}
