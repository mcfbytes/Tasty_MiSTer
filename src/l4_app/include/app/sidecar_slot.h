// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <optional>
#include <string_view>

#include "app/link_tx_channel.h"
#include "app/negotiated_core.h"
#include "app/owner_tally.h"
#include "cores/ladder_host.h"
#include "cores/sidecar_builder.h"
#include "cores/sidecar_send.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

class SidecarSlot {
    TASTY_SEAT_RESIDENT(Main);

public:
    SidecarSlot() noexcept = default;
    SidecarSlot(const SidecarSlot&) = delete;
    SidecarSlot& operator=(const SidecarSlot&) = delete;

    void make(const NegotiatedCore& negotiated);

    void withdraw() noexcept;

    void cancel() noexcept;

    [[nodiscard]] bool present() const noexcept { return builder_ != nullptr; }

    [[nodiscard]] bool plan(std::string_view pick, proto::WideIoIndex index, const svc::Vfs& vfs);

    [[nodiscard]] bool live() const noexcept { return send_.has_value(); }

    [[nodiscard]] bool wants_pass(const LinkTxChannel<SeatTag::Main>& inbox) const noexcept;

    void step(cores::ILadderHost& host, OwnerTally& tally);

private:
    std::unique_ptr<cores::ISidecarBuilder> builder_{};
    std::optional<cores::SidecarSend> send_{};
};

}
