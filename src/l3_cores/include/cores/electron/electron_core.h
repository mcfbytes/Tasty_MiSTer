// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/core_support.h"
#include "cores/stream_load_host.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::cores::electron {

class ElectronCore final : public Core {
    TASTY_SEAT_RESIDENT(RT);

public:
    ElectronCore(const CoreProfile& p, const HostServices& h) : Core(p, h) {}

    [[nodiscard]] std::uint64_t bytes_sent() const noexcept { return stream_.bytes(); }

    [[nodiscard]] std::uint32_t transfers() const noexcept { return stream_.transfers(); }

    [[nodiscard]] proto::FioIndexByte last_index() const noexcept { return stream_.last_index(); }

private:
    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt& a) override;

    [[nodiscard]] std::uint64_t on_last_tx_bytes() const noexcept override {
        return stream_.bytes();
    }

    [[nodiscard]] infra::OptRef<IStreamLoad> on_stream_load() noexcept override {
        return infra::OptRef<IStreamLoad>{stream_};
    }

    StreamLoadHost stream_;
};

}
