// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

#include "cores/core_support.h"
#include "cores/pcm_feed.h"
#include "cores/pcm_source.h"
#include "cores/pcm_wire_rows.h"
#include "cores/stream_load.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"

namespace mister::cores {

class MegaDriveCore final : public Core, public IPcmWireRows, public IStreamLoad {
    TASTY_SEAT_RESIDENT(RT);

public:
    MegaDriveCore(const CoreProfile& p, const HostServices& h) : Core(p, h) {}

    static constexpr std::size_t kChunkBytes = 4096;

    static constexpr std::uint16_t kCmdStatus = 0x60;
    static constexpr std::uint16_t kCmdAck = 0x61;
    static constexpr std::uint16_t kCmdAudio = 0x62;
    static constexpr std::uint8_t kFlagPlay = 0x01;
    static constexpr std::uint8_t kFlagStop = 0x02;
    static constexpr std::uint8_t kFlagResume = 0x04;
    static constexpr std::uint8_t kFlagVolume = 0x08;
    static constexpr std::uint8_t kFlagLoop = 0x10;

    static constexpr std::uint32_t kAckTicks = 75;

    [[nodiscard]] Ex<proto::SessionParams> stream_opening(IoIndex index) override;
    void stream_closed(const StreamLoadEnd& end) noexcept override;

    [[nodiscard]] std::uint64_t bytes_sent() const noexcept { return bytes_sent_; }
    [[nodiscard]] std::uint32_t transfers() const noexcept { return transfers_; }
    [[nodiscard]] std::uint32_t status_reads() const noexcept { return status_reads_; }
    [[nodiscard]] std::uint32_t acks() const noexcept { return acks_; }
    [[nodiscard]] std::uint32_t exchanges() const noexcept { return exchanges_; }
    [[nodiscard]] std::uint32_t wire_faults() const noexcept { return wire_faults_; }
    [[nodiscard]] std::uint32_t feed_arms() const noexcept { return feed_arms_; }
    [[nodiscard]] std::uint32_t feed_refusals() const noexcept { return feed_refusals_; }

    [[nodiscard]] std::uint32_t feed_deferrals() const noexcept { return feed_deferrals_; }
    [[nodiscard]] bool feed_rearm_pending() const noexcept { return feed_rearm_pending_; }

    [[nodiscard]] std::uint32_t play_refusals() const noexcept { return play_refusals_; }
    [[nodiscard]] std::uint32_t ack_timeouts() const noexcept { return ack_timeouts_; }

    [[nodiscard]] bool ack_pending() const noexcept { return ack_pending_; }
    [[nodiscard]] std::uint8_t current_track() const noexcept { return current_track_; }
    [[nodiscard]] bool paused() const noexcept { return paused_; }

private:
    struct Status {
        std::uint8_t track = 0;
        std::uint8_t flags = 0;
        std::uint8_t fade = 0;
        std::uint8_t volume = 0;
    };

    [[nodiscard]] Ex<void> do_init(proto::CoreSession& s) override;
    void on_set_pending_file_ext(std::string_view ext) noexcept override;
    void on_set_pending_file_path(std::string_view path) noexcept override;
    [[nodiscard]] IStreamLoad* on_stream_load() noexcept override { return this; }
    [[nodiscard]] Ex<void> on_mount(IoIndex slot, const MountedPath& p) override;
    [[nodiscard]] std::uint64_t on_last_tx_bytes() const noexcept override { return bytes_sent_; }
    [[nodiscard]] std::uint32_t on_last_tx_crc() const noexcept override { return tx_crc_; }
    [[nodiscard]] IPcmWireRows* on_pcm_wire_rows() noexcept override { return this; }

    void service_pcm_tick() noexcept override;

    [[nodiscard]] IPcmFeed* feed() noexcept;
    void rearm_feed_();

    void retry_feed_(IPcmFeed& fd) noexcept;

    [[nodiscard]] bool read_status_(Status& out) noexcept;
    void send_ack_(std::uint8_t track, bool playing) noexcept;
    void pump_(IPcmFeed& fd) noexcept;

    [[nodiscard]] bool settle_ack_(IPcmFeed& fd) noexcept;
    void arm_play_(IPcmFeed& fd, std::uint8_t track, bool loop) noexcept;

    void answer_no_play_() noexcept;

    FixedStr<8, StrFit::Clip> ext_{};
    FixedStr<1024, StrFit::Clip> path_{};
    std::uint64_t bytes_sent_ = 0;
    std::uint32_t tx_crc_ = 0;
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
    std::uint16_t last_index_ = 0;

    std::uint8_t current_track_ = 0;
    std::uint8_t ack_track_ = 0;
    std::uint32_t served_before_ = 0;

    std::uint32_t ack_ticks_ = 0;
    bool ack_pending_ = false;
    bool paused_ = false;

    std::unique_ptr<IPcmSource> pending_feed_{};
    bool feed_rearm_pending_ = false;

    std::uint32_t rearm_ticks_ = 0;
};

}  // namespace mister::cores
