// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/core_grant.h"
#include "cores/core.h"
#include "cores/lba_window_geometry.h"
#include "cores/slot_servant_grant.h"
#include "cores/slot_servants.h"
#include "cores/stream_load_host.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/storage_backend.h"

namespace mister::cores::mac {

class MacCore final : public Core, public ISlotServants {
    TASTY_SEAT_RESIDENT(RT);

public:
    MacCore(const CoreProfile& p, const CoreGrant& g);

    [[nodiscard]] infra::OptRef<svc::IStorageBackend> servant(
        proto::SlotIndex slot
    ) noexcept override {
        TASTY_SEAT_BODY(MacCore);
        return servants_.servant(slot);
    }

    [[nodiscard]] const proto::IWindowGeometry& window_geometry() const noexcept override {
        TASTY_SEAT_BODY(MacCore);
        return geometry_;
    }

private:
    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt& a) override;

    std::uint64_t on_last_tx_bytes() const noexcept override { return stream_.bytes(); }

    svc::Crc32 on_last_tx_crc() const noexcept override { return stream_.crc(); }

    [[nodiscard]] infra::OptRef<IStreamLoad> on_stream_load() noexcept override {
        return infra::OptRef<IStreamLoad>{stream_};
    }

    [[nodiscard]] Ex<void> on_mount(IoIndex slot, const MountedPath& p) override;

    [[nodiscard]] infra::OptRef<ISlotServants> on_slot_servants() noexcept override {
        return infra::OptRef<ISlotServants>{*this};
    }

    StreamLoadHost stream_;
    LbaWindowGeometry geometry_;

    SlotServantGrant servants_;
};

}
