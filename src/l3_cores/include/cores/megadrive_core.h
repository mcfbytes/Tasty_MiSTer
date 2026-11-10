// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "cores/core_support.h"
#include "cores/md_cmd_flags.h"
#include "cores/owed_answer.h"
#include "cores/pcm_feed.h"
#include "cores/pcm_wire_rows.h"
#include "cores/stream_load.h"
#include "cores/stream_tx_record.h"
#include "hal/types.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "proto/uio_opcodes.h"
#include "svc/types.h"

namespace mister::cores {

namespace detail {

struct PlayAct {
    svc::TrackNumber track{};
    bool loop = false;
    std::uint32_t served_before = 0;
};

struct SheetAct {
    FixedStr<1024, StrFit::Clip> name{};
};

}

class MegaDriveCore final : public Core, public IPcmWireRows, public IStreamLoad {
    TASTY_SEAT_RESIDENT(RT);

public:
    MegaDriveCore(const CoreProfile& p, const HostServices& h) : Core(p, h) {}

    static constexpr std::uint16_t kCmdStatus = 0x60;
    static constexpr std::uint16_t kCmdAck = static_cast<std::uint16_t>(proto::UioOp::DmaWrite);
    static constexpr std::uint16_t kCmdAudio = static_cast<std::uint16_t>(proto::UioOp::DmaRead);

    static constexpr std::uint32_t kAckTicks = 75;

    [[nodiscard]] Ex<proto::SessionParams> stream_opening(IoIndex index) override;
    void stream_closed(const StreamLoadEnd& end) noexcept override;

    [[nodiscard]] std::uint64_t bytes_sent() const noexcept { return tx_.bytes(); }

    [[nodiscard]] std::uint32_t transfers() const noexcept { return transfers_; }

    [[nodiscard]] std::uint32_t status_reads() const noexcept { return status_reads_; }

    [[nodiscard]] std::uint32_t acks() const noexcept { return acks_; }

    [[nodiscard]] std::uint32_t exchanges() const noexcept { return exchanges_; }

    [[nodiscard]] std::uint32_t wire_faults() const noexcept { return wire_faults_; }

    [[nodiscard]] std::uint32_t feed_arms() const noexcept { return feed_arms_; }

    [[nodiscard]] std::uint32_t feed_refusals() const noexcept { return feed_refusals_; }

    [[nodiscard]] std::uint32_t feed_deferrals() const noexcept { return feed_deferrals_; }

    [[nodiscard]] bool feed_rearm_pending() const noexcept { return sheet_.owed(); }

    [[nodiscard]] std::uint32_t play_refusals() const noexcept { return play_refusals_; }

    [[nodiscard]] std::uint32_t ack_timeouts() const noexcept { return ack_timeouts_; }

    [[nodiscard]] svc::Tally stranded_drops() const noexcept {
        return play_.stranded_drops().sum(sheet_.stranded_drops());
    }

    [[nodiscard]] bool ack_pending() const noexcept { return play_.owed(); }

    [[nodiscard]] std::optional<svc::TrackNumber> current_track() const noexcept {
        return current_track_;
    }

    [[nodiscard]] bool paused() const noexcept { return paused_; }

private:
    struct Status {

        std::optional<svc::TrackNumber> track{};
        md::CmdFlags flags{};
        std::uint8_t fade = 0;
        std::uint8_t volume = 0;

        [[nodiscard]] static Status of(hal::SpiWord word0, hal::SpiWord word1) noexcept;
    };

    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt& a) override;
    void on_set_pending_file_ext(std::string_view ext) noexcept override;
    void on_set_pending_file_path(std::string_view path) noexcept override;

    [[nodiscard]] infra::OptRef<IStreamLoad> on_stream_load() noexcept override {
        return infra::OptRef<IStreamLoad>{*this};
    }

    [[nodiscard]] Ex<void> on_mount(IoIndex slot, const MountedPath& p) override;

    [[nodiscard]] std::uint64_t on_last_tx_bytes() const noexcept override { return tx_.bytes(); }

    [[nodiscard]] svc::Crc32 on_last_tx_crc() const noexcept override { return tx_.crc(); }

    [[nodiscard]] infra::OptRef<IPcmWireRows> on_pcm_wire_rows() noexcept override {
        return infra::OptRef<IPcmWireRows>{*this};
    }

    void service_pcm_tick() noexcept override;

    [[nodiscard]] infra::OptRef<IPcmFeed> feed() noexcept;
    void rearm_feed_();

    void retry_feed_(IPcmFeed& fd) noexcept;

    [[nodiscard]] std::optional<Status> read_status_() noexcept;
    void send_ack_(std::optional<svc::TrackNumber> track, bool playing) noexcept;
    void pump_(IPcmFeed& fd) noexcept;

    void hold_play_() noexcept;
    void arm_play_(IPcmFeed& fd, svc::TrackNumber track, bool loop) noexcept;

    void answer_no_play_() noexcept;

    [[nodiscard]] proto::ActKey mint_() noexcept { return next_gen_ = next_gen_.next(); }

    StreamTxRecord tx_;
    FixedStr<1024, StrFit::Clip> path_{};
    std::uint32_t transfers_ = 0;
    std::uint32_t status_reads_ = 0;
    std::uint32_t acks_ = 0;
    std::uint32_t exchanges_ = 0;
    std::uint32_t wire_faults_ = 0;
    std::uint32_t feed_arms_ = 0;
    std::uint32_t feed_refusals_ = 0;
    std::uint32_t feed_deferrals_ = 0;
    std::uint32_t play_refusals_ = 0;
    std::uint32_t ack_timeouts_ = 0;
    proto::IoIndex last_index_{};

    std::optional<svc::TrackNumber> current_track_{};
    bool paused_ = false;

    proto::ActKey next_gen_{};
    detail::OwedAnswer<detail::PlayAct, kAckTicks> play_;
    detail::OwedAnswer<detail::SheetAct, kAckTicks> sheet_;
};

}
