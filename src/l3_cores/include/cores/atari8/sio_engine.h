// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "cores/atari8/atari8_status.h"
#include "cores/atari8/sio_command.h"
#include "cores/atari8/sio_drives.h"
#include "cores/atari8/sio_frame.h"
#include "cores/atari8/sio_frame_reader.h"
#include "cores/atari8/sio_reply.h"
#include "cores/atari8/sio_terms.h"
#include "cores/atari8/sio_types.h"
#include "infra/byte_count.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "proto/mailbox_act.h"
#include "proto/mailbox_result.h"
#include "proto/mailbox_run.h"
#include "proto/types.h"

namespace mister::cores::atari8 {

struct SioRunCount {
    std::uint16_t v = 0;
    using affine_count = SioRunCount;

    [[nodiscard]] static constexpr SioRunCount ran(proto::ReplyCount kept) noexcept {
        return SioRunCount{static_cast<std::uint16_t>(kept.v)};
    }

    friend constexpr bool operator==(SioRunCount, SioRunCount) = default;
    friend constexpr auto operator<=>(SioRunCount, SioRunCount) = default;
};

struct SioRunIndex {
    std::uint16_t v = 0;
    using affine_count = SioRunCount;
    using affine_index = SioRunIndex;
    friend constexpr bool operator==(SioRunIndex, SioRunIndex) = default;
    friend constexpr auto operator<=>(SioRunIndex, SioRunIndex) = default;
};

class SioEngine {
    TASTY_SEAT_RESIDENT(Pcm);

public:
    using ActBytes = std::span<std::uint8_t, proto::kMailboxActBytes>;
    using Instant = proto::MailboxAct::NotBefore;

    enum class Step : std::uint8_t {
        Listen,
        Drain,
        Switch,
        Ack,
        Complete,
        Data,
        Receive,
        WriteAck,
        WriteComplete,
        Tail,
        Divisor,
        BootLead,
        BootWait,
        BootTail,
    };

    static constexpr std::chrono::microseconds kPreAck{100};
    static constexpr std::chrono::microseconds kPreAckTimed{3220};
    static constexpr std::chrono::microseconds kPreComplete{600};
    static constexpr std::chrono::microseconds kPreData{150};
    static constexpr std::chrono::microseconds kPreWriteAck{850};

    void begin_core() noexcept;

    void order_reset() noexcept;

    void order_boot() noexcept;

    void reboot_ordered() noexcept;
    [[nodiscard]] bool wants_reboot() const noexcept;

    void on_rx(SioRxStatus status) noexcept;

    [[nodiscard]] proto::MailboxAct next(ActBytes bytes, const SioTerms& t);

    void settled(const proto::MailboxResult& result, const SioTerms& t);

    [[nodiscard]] Step step() const noexcept;
    [[nodiscard]] const SioFrame& frame() const noexcept;

    static constexpr SioRunCount kMostStaged = SioRunCount{SioDrives::kBufferBytes.size() + 2};

private:
    struct Staged {
        infra::IndexedArray<proto::MailboxRunCommand, kMostStaged.v, SioRunIndex> commands{};
        SioRunCount size{};
        SioRunIndex cursor{};
        Instant not_before{};
        bool guarded = false;

        void add(const proto::MailboxRunCommand& command) noexcept {
            if (size == kMostStaged)
                return;
            commands[infra::affine::end_of<SioRunIndex>(size)] = command;
            size += SioRunCount{1};
        }

        void advance(proto::ReplyCount kept) noexcept {
            const SioRunIndex end = infra::affine::end_of<SioRunIndex>(size);
            const SioRunIndex next = cursor + SioRunCount::ran(kept);
            cursor = next < end ? next : end;
        }

        [[nodiscard]] bool done() const noexcept {
            return cursor == infra::affine::end_of<SioRunIndex>(size);
        }
    };

    void stage_(Step step, Instant not_before, bool guarded) noexcept;
    void transmit_(std::byte byte) noexcept;
    [[nodiscard]] proto::MailboxAct staged_act_(ActBytes bytes) noexcept;
    [[nodiscard]] proto::MailboxAct drain_act_(ActBytes bytes) noexcept;
    [[nodiscard]] proto::MailboxAct receive_act_(ActBytes bytes) noexcept;
    void drained_(const proto::MailboxResult& result, const SioTerms& t);
    void received_(const proto::MailboxResult& result, const SioTerms& t);
    void frame_complete_(SioDivisor divisor, const SioTerms& t);
    void staged_done_(const SioTerms& t);
    void run_verb_(const SioTerms& t);
    void stage_data_(const SioTerms& t) noexcept;
    void stage_tail_() noexcept;
    void forget_rx_(SioWordCount popped) noexcept;
    [[nodiscard]] SioCall call_(const SioTerms& t) const noexcept;

    Step step_ = Step::Listen;
    Staged staged_{};
    SioWordCount rx_known_{};
    proto::ReplyCount pops_{};
    bool asked_divisor_ = false;
    SioFrameReader reader_{};
    SioFrame frame_{};
    SioUnit unit_{};
    std::optional<SioCommand> verb_{};
    SioReply reply_{};
    std::array<std::byte, SioDrives::kBufferBytes.size()> buffer_{};
    infra::ByteCount expected_{};
    infra::ByteCount received_bytes_{};
    infra::IndexedArray<std::byte, SioDrives::kBufferBytes.size() + 1, infra::ByteOffset> data_{};
};

}
