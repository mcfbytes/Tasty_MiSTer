// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "app/board_ops.h"
#include "app/durable_write_service.h"
#include "hal/spi_transport.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/log_lane.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::svc {
class Vfs;
}

namespace mister::cores {
class Core;
class ISaveUpload;
}

namespace mister::app {

class NvramUpload {
    TASTY_SEAT_RESIDENT(RT);

public:
    struct Tally {
        TASTY_SEAT_EXEMPT(component);
        std::uint32_t save_uploads = 0;
        std::uint32_t save_upload_errors = 0;
    };

    struct SaveCtx {
        infra::OptRef<cores::Core> core{};
        bool fenced = false;
    };

    NvramUpload(
        hal::ISpiTransport& link,
        infra::OptRef<const svc::Vfs> vfs,
        DurableWriteService& writes,
        infra::OptRef<ISleeper> sleeper,
        xthread::LogLane& log_lane
    ) noexcept
        : link_(link),
          vfs_(vfs),
          writes_(writes),
          sleeper_(sleeper),
          log_lane_(log_lane) {}

    [[nodiscard]] Ex<std::size_t> flush_save_upload(const SaveCtx& ctx);

    [[nodiscard]] Ex<std::size_t> save_upload_now(const SaveCtx& ctx);

    [[nodiscard]] bool save_upload_counted(const SaveCtx& ctx) noexcept;

    void drain_write_verdicts();

    void settle_save_lane();

    void settle_writes() noexcept;

    [[nodiscard]] bool writes_settled() const noexcept { return writes_.in_flight() == 0; }

    [[nodiscard]] const Tally& tally() const noexcept { return tally_; }

    void count_error() noexcept { ++tally_.save_upload_errors; }

    void count_upload() noexcept { ++tally_.save_uploads; }

private:
    [[nodiscard]] Ex<void> arm_(std::string_view rel, std::span<const std::byte> bytes);
    [[nodiscard]] Ex<std::size_t> write_save_upload_(cores::ISaveUpload& up);

    void sleep_ms(unsigned ms);

    hal::ISpiTransport& link_;
    infra::OptRef<const svc::Vfs> vfs_{};
    DurableWriteService& writes_;
    infra::OptRef<ISleeper> sleeper_{};
    xthread::LogLane& log_lane_;
    Tally tally_{};
};

}
