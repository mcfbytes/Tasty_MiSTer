// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <utility>

#include "app/disk_attach_seq.h"
#include "app/disk_relay.h"
#include "cores/disk_port.h"
#include "cores/disk_servant.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/counter.h"
#include "infra/xthread/polled.h"
#include "infra/xthread/wake_flag.h"
#include "proto/disk_protocol.h"
#include "proto/disk_protocol_row.h"
#include "proto/types.h"
#include "svc/io_coworker.h"

namespace mister::app {

class DiskRelays final : public svc::IIoCoworker {
    TASTY_SEAT_MEDIATOR(RT, Io);

public:
    using Relays = std::array<DiskRelay, std::tuple_size_v<decltype(proto::kDiskProtocols)>>;

    static constexpr proto::FioWordCount kMaxRoundWords = DiskRelay::kMaxRoundWords;

    explicit DiskRelays(xthread::Polled polled) noexcept
        : relays_(make_(polled, std::make_index_sequence<std::tuple_size_v<Relays>>{})) {}

    explicit DiskRelays(xthread::WakeFlag& io_wake) noexcept
        : relays_(make_(io_wake, std::make_index_sequence<std::tuple_size_v<Relays>>{})) {}

    void seal_boot_pump() noexcept { sealed_ = true; }

    void pump_on_caller() noexcept;

    [[nodiscard]] cores::DiskPorts ports() noexcept;

    DiskAttachSeq attach(const cores::DiskServantSet& servants) noexcept;

    DiskAttachSeq detach() noexcept;

    [[nodiscard]] bool settle_attach(DiskAttachSeq order) noexcept;

    [[nodiscard]] proto::FioWordCount take_round_words() noexcept;

    void serve() noexcept override;
    [[nodiscard]] bool idle() const noexcept override;

    [[nodiscard]] infra::OptRef<DiskRelay> relay(proto::DiskProtocol protocol) noexcept;

    [[nodiscard]] std::uint32_t sealed_pumps() const noexcept { return sealed_pumps_.get(); }

private:
    template <class Wake, std::size_t... I>
    [[nodiscard]] static Relays make_(Wake& wake, std::index_sequence<I...>) noexcept {
        return Relays{DiskRelay{wake, proto::kDiskProtocols[I]}...};
    }

    bool sealed_ = false;
    xthread::Counter sealed_pumps_{};

    DiskAttachSeq order_{};
    Relays relays_;
};

}
