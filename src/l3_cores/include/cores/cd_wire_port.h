// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "cores/cd_profile.h"
#include "cores/cd_sector_egress.h"
#include "cores/core_profile.h"
#include "cores/host_services.h"
#include "hal/spi_transport.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/image_sink.h"
#include "proto/reset_terms.h"
#include "proto/spi_fio_queue.h"

namespace mister::cores {

class CdEngine;

class CdWirePort {
    TASTY_SEAT_RESIDENT(RT);

public:
    CdWirePort(const CdProfile& cd, const HostServices& host);
    ~CdWirePort();
    CdWirePort(const CdWirePort&) = delete;
    CdWirePort& operator=(const CdWirePort&) = delete;

    [[nodiscard]] Ex<void> send_data(
        IoIndex io_index,
        std::span<const std::byte> payload,
        proto::IImageSink& sink
    );
    [[nodiscard]] Ex<void> bracket_inline(
        IoIndex io_index,
        std::span<const std::byte> payload,
        proto::IImageSink& sink
    );

    [[nodiscard]] Ex<void> send_status(std::span<const std::uint16_t> words);
    bool can_send_data(std::uint16_t selector);
    [[nodiscard]] bool egress_idle() const noexcept;
    [[nodiscard]] std::uint32_t egress_abandons() const noexcept;
    [[nodiscard]] Ex<void> submit_sector(const ICdSectorEgress::Act& act);

    void service_command_edge(CdEngine& eng) noexcept;

    void count_fault() noexcept { ++wire_faults_; }

    void declare_resets(const proto::ResetTerms& terms) noexcept;

    [[nodiscard]] bool queued() const noexcept { return fio_queue_.has_value(); }

    [[nodiscard]] std::uint32_t wire_faults() const noexcept { return wire_faults_; }

    [[nodiscard]] bool cdda_capture_armed() const noexcept { return cdda_cap_ != nullptr; }

private:
    struct CddaCapture;
    std::unique_ptr<CddaCapture> cdda_cap_;

    const CdProfile& cd_;
    hal::ISpiTransport& link_;
    infra::OptRef<proto::SpiFioQueue> fio_queue_;
    std::uint32_t wire_faults_ = 0;
};

}
