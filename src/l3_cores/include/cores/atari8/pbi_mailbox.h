// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "cores/atari8/atari8_status2.h"
#include "cores/atari8/pbi_drive_mode.h"
#include "cores/atari8/pbi_types.h"
#include "cores/atari8/sio_command.h"
#include "cores/atari8/sio_drives.h"
#include "cores/atari8/sio_frame.h"
#include "cores/atari8/sio_reply.h"
#include "cores/atari8/sio_terms.h"
#include "cores/atari8/sio_types.h"
#include "infra/byte_count.h"
#include "infra/seat.h"
#include "proto/mailbox_act.h"
#include "proto/mailbox_result.h"
#include "proto/mailbox_run.h"

namespace mister::cores::atari8 {

class PbiMailbox {
    TASTY_SEAT_RESIDENT(Pcm);

public:
    using ActBytes = std::span<std::uint8_t, proto::kMailboxActBytes>;

    enum class Step : std::uint8_t {
        Idle,
        Poll,
        Options,
        InitConfig,
        InitFlag,
        LedOn,
        Request,
        Data,
        Prefetch,
        Reply,
        Length,
        Result,
        LedOff,
        Answer,
    };

    void begin_core() noexcept;

    [[nodiscard]] bool wants(const SioTerms& t) const noexcept;

    [[nodiscard]] proto::MailboxAct next(ActBytes bytes, const SioTerms& t);

    void settled(const proto::MailboxResult& result, ActBytes bytes, const SioTerms& t);

    [[nodiscard]] Step step() const noexcept;

    static constexpr infra::ByteCount kHostPrefetch{0x60};

private:
    void request_read_(const SioTerms& t);
    void begin_verb_(const SioTerms& t);
    void run_verb_(const SioTerms& t);
    void finish_(PbiDriveMode mode) noexcept;
    [[nodiscard]] SioCall call_(const SioTerms& t) const noexcept;
    [[nodiscard]] infra::ByteCount data_length_() const noexcept;

    Step step_ = Step::Idle;
    PbiMailboxBlock mailbox_{};
    Atari8Status2 modes_{};
    Atari8Status2 options_{};
    HostDcb request_{};
    std::array<std::byte, SioDrives::kBufferBytes.size()> buffer_{};
    infra::ByteCount moved_{};
    std::array<std::byte, kHostPrefetch.size()> host_{};
    infra::ByteCount host_bytes_{};
    SioFrame frame_{};
    SioUnit unit_{};
    std::optional<SioCommand> verb_{};
    SioReply reply_{};
};

}
