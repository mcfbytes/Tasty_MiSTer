// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/megadrive_core.h"

#include <memory>
#include <utility>

#include "cores/core_window.h"
#include "cores/file_tx.h"
#include "cores/manifests/megadrive.h"
#include "cores/mdp_source.h"
#include "cores/mounted_path.h"
#include "hal/selected.h"
#include "hal/spi_transport.h"

namespace mister::cores {

Ex<void> MegaDriveCore::do_init(proto::CoreSession&) { return {}; }

void MegaDriveCore::on_set_pending_file_ext(std::string_view ext) noexcept {
    (void)ext_.assign(ext);
}

void MegaDriveCore::on_set_pending_file_path(std::string_view path) noexcept {
    (void)path_.assign(path);
}

IPcmFeed* MegaDriveCore::feed() noexcept {
    ICoreWindow* w = host().windows.named(manifests::kMdPlusWindows[0].region.name);
    return w == nullptr ? nullptr : w->pcm_feed();
}

void MegaDriveCore::rearm_feed_() {
    IPcmFeed* fd = feed();
    if (fd == nullptr) return;
    ++feed_arms_;
    ack_pending_ = false;
    paused_ = false;
    current_track_ = 0;
    rearm_ticks_ = 0;

    pending_feed_ =
        path_.empty() ? nullptr : std::make_unique<mdp::CueWavSource>(host().vfs, path_.view());
    feed_rearm_pending_ = true;
    retry_feed_(*fd);
}

void MegaDriveCore::retry_feed_(IPcmFeed& fd) noexcept {
    if (!feed_rearm_pending_) return;
    if (!fd.ready_for_source()) {
        ++feed_deferrals_;
        return;
    }
    feed_rearm_pending_ = false;
    if (auto r = fd.set_source(std::move(pending_feed_)); !r) ++feed_refusals_;
    pending_feed_.reset();
}

Ex<proto::SessionParams> MegaDriveCore::stream_opening(IoIndex) {
    TASTY_SEAT_BODY(MegaDriveCore);
    bytes_sent_ = 0;
    tx_crc_ = 0;
    proto::SessionParams params{};
    params.ext = ext_.view();
    return params;
}

void MegaDriveCore::stream_closed(const StreamLoadEnd& end) noexcept {
    TASTY_SEAT_BODY(MegaDriveCore);
    last_index_ = static_cast<std::uint16_t>(end.index.v);
    ++transfers_;
    if (!end.ok) return;
    bytes_sent_ = end.bytes;
    tx_crc_ = end.crc;

    rearm_feed_();
}

Ex<void> MegaDriveCore::on_mount(IoIndex, const MountedPath&) { return unimplemented(ERR_SITE()); }

bool MegaDriveCore::read_status_(Status& out) noexcept {
    hal::ISpiTransport& link = host().link;
    hal::Selected cs(link, hal::ChipSelect::Io);
    const auto w0 = link.transfer(hal::SpiWord{kCmdStatus});
    if (!w0) {
        ++wire_faults_;
        return false;
    }
    const auto w1 = link.transfer(hal::SpiWord{0});
    if (!w1) {
        ++wire_faults_;
        return false;
    }
    ++status_reads_;
    out.track = static_cast<std::uint8_t>((w0->v >> 8) & 0xFFu);
    out.flags = static_cast<std::uint8_t>(w0->v & 0xFFu);
    out.fade = static_cast<std::uint8_t>((w1->v >> 8) & 0xFFu);

    out.volume = static_cast<std::uint8_t>(w1->v & 0xFFu);
    return true;
}

void MegaDriveCore::send_ack_(std::uint8_t track, bool playing) noexcept {
    hal::ISpiTransport& link = host().link;
    hal::Selected cs(link, hal::ChipSelect::Io);
    if (auto r = link.transfer(hal::SpiWord{kCmdAck}); !r) {
        ++wire_faults_;
        return;
    }
    const std::uint16_t word =
        static_cast<std::uint16_t>((static_cast<unsigned>(track) << 8) | (playing ? 1u : 0u));
    if (auto r = link.transfer(hal::SpiWord{word}); !r) {
        ++wire_faults_;
        return;
    }
    ++acks_;
}

void MegaDriveCore::pump_(IPcmFeed& fd) noexcept {
    const PcmFeedState s = fd.state();
    if (!s.playing || paused_) return;
    hal::ISpiTransport& link = host().link;
    std::uint16_t read_point = 0;
    {
        hal::Selected cs(link, hal::ChipSelect::Io);
        const auto rd = link.transfer(hal::SpiWord{kCmdAudio});
        if (!rd) {
            ++wire_faults_;
            return;
        }
        read_point = static_cast<std::uint16_t>(rd->v);
        const auto w = link.transfer(hal::SpiWord{static_cast<std::uint16_t>(s.write_point)});
        if (!w) {
            ++wire_faults_;
            return;
        }
    }
    ++exchanges_;

    fd.observe_read_point(read_point);
}

bool MegaDriveCore::settle_ack_(IPcmFeed& fd) noexcept {
    if (!ack_pending_) return false;
    const PcmFeedState s = fd.state();
    if (s.served == served_before_ || !s.primed) return false;
    current_track_ = s.playing ? ack_track_ : 0;
    ack_pending_ = false;
    send_ack_(current_track_, s.playing);
    return true;
}

void MegaDriveCore::answer_no_play_() noexcept {
    ack_pending_ = false;
    current_track_ = 0;
    send_ack_(0, false);
}

void MegaDriveCore::arm_play_(IPcmFeed& fd, std::uint8_t track, bool loop) noexcept {
    paused_ = false;

    if (feed_rearm_pending_ || !fd.can_serve()) {
        ++play_refusals_;
        answer_no_play_();
        return;
    }

    served_before_ = fd.state().served;
    ack_track_ = track;
    ack_ticks_ = 0;
    ack_pending_ = true;
    if (auto r = fd.play(track, loop); !r) {
        ++play_refusals_;
        answer_no_play_();
    }
}

void MegaDriveCore::service_pcm_tick() noexcept {
    TASTY_SEAT_BODY(MegaDriveCore);
    IPcmFeed* fdp = feed();
    if (fdp == nullptr) return;
    IPcmFeed& fd = *fdp;
    retry_feed_(fd);

    Status st{};
    if (!read_status_(st)) return;

    if (ack_pending_) {
        if (settle_ack_(fd)) {
            pump_(fd);
        } else if (++ack_ticks_ >= kAckTicks) {
            ++ack_timeouts_;
            answer_no_play_();
        }
        return;
    }
    if (st.flags == 0) {
        pump_(fd);
        return;
    }

    if ((st.flags & kFlagPlay) != 0) {
        if (st.track == 0) {

            send_ack_(current_track_, fd.state().playing);
            return;
        }

        if (feed_rearm_pending_ && ++rearm_ticks_ < kAckTicks) return;
        arm_play_(fd, st.track, (st.flags & kFlagLoop) != 0);
        return;
    }
    if ((st.flags & kFlagStop) != 0) {

        if (st.fade == 0) {
            paused_ = true;
            if (auto r = fd.stop(); !r) paused_ = false;
        }
    } else if ((st.flags & kFlagResume) != 0) {
        paused_ = false;
        (void)fd.resume();
    }
    send_ack_(current_track_, fd.state().playing);
    pump_(fd);
}

}  // namespace mister::cores
