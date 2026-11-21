// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/megadrive/megadrive_core.h"
#include "cores/core_adopt.h"

#include "cores/core_window.h"
#include "cores/file_tx.h"
#include "cores/megadrive/megadrive_windows.h"
#include "cores/mounted_path.h"
#include "hal/selected.h"
#include "hal/spi_transport.h"
#include "infra/prelude.h"

namespace mister::cores::megadrive {

Ex<void> MegaDriveCore::do_adopt(const CoreAdopt&) {

    return {};
}

infra::OptRef<IPcmFeed> MegaDriveCore::feed() noexcept {
    const infra::OptRef<ICoreWindow> w = host().windows.named(kMdPlusWindows[0].region.name);
    return w ? w->pcm_feed() : infra::OptRef<IPcmFeed>{};
}

void MegaDriveCore::rearm_feed_() {
    const infra::OptRef<IPcmFeed> fd = feed();
    if (!fd)
        return;
    ++feed_arms_;

    play_.stranded();
    paused_ = false;
    current_track_.reset();

    detail::SheetAct next{};
    (void)next.name.assign(path_.view());
    sheet_.arm(mint_(), next);
    retry_feed_(*fd);
}

void MegaDriveCore::retry_feed_(IPcmFeed& fd) noexcept {
    if (!sheet_.owed())
        return;
    if (!fd.ready_for_source()) {
        ++feed_deferrals_;
        return;
    }
    const std::string_view name = sheet_.act().name.view();
    if (auto r = fd.set_sheet(name); !r) {
        ++feed_refusals_;
        sheet_.declined();
        return;
    }
    sheet_.answered();
}

Ex<proto::SessionParams> MegaDriveCore::stream_opening(const StreamAsk& ask) {
    TASTY_SEAT_BODY(MegaDriveCore);
    tx_.opened(ask);
    (void)path_.assign(ask.path);
    proto::SessionParams params{};
    params.ext = tx_.ext();
    return params;
}

void MegaDriveCore::stream_closed(const StreamLoadEnd& end) noexcept {
    TASTY_SEAT_BODY(MegaDriveCore);
    last_index_ = end.index;
    ++transfers_;
    tx_.closed(end);
    if (!end.ok)
        return;

    rearm_feed_();
}

Ex<void> MegaDriveCore::on_mount(IoIndex, const MountedPath&) {

    return unimplemented(ERR_SITE());
}

MegaDriveCore::Status MegaDriveCore::Status::of(Words w) noexcept {
    Status s{};
    const std::uint8_t track_byte = w.track_flags.high_byte();

    if (track_byte != 0)
        s.track = svc::TrackNumber::truncating(track_byte);
    s.flags = md::CmdFlags::of_word(w.track_flags);
    s.fade = w.fade_volume.high_byte();

    s.volume = w.fade_volume.low_byte();
    return s;
}

std::optional<MegaDriveCore::Status> MegaDriveCore::read_status_() noexcept {
    hal::ISpiTransport& link = host().link;
    hal::Selected cs(link, hal::ChipSelect::Io);
    const auto w0 = link.transfer(hal::SpiWord{kCmdStatus});
    if (!w0) {
        ++wire_faults_;
        return std::nullopt;
    }
    const auto w1 = link.transfer(hal::SpiWord{0});
    if (!w1) {
        ++wire_faults_;
        return std::nullopt;
    }
    ++status_reads_;
    return Status::of({.track_flags = *w0, .fade_volume = *w1});
}

void MegaDriveCore::send_ack_(std::optional<svc::TrackNumber> track, bool playing) noexcept {
    hal::ISpiTransport& link = host().link;
    hal::Selected cs(link, hal::ChipSelect::Io);
    if (auto r = link.transfer(hal::SpiWord{kCmdAck}); !r) {
        ++wire_faults_;
        return;
    }
    constexpr hal::SpiWord kPlaying{1};
    constexpr hal::SpiWord kPaused{0};
    const hal::SpiWord base = playing ? kPlaying : kPaused;
    const hal::SpiWord word = track ? track->in_high_byte(base) : base;
    if (auto r = link.transfer(word); !r) {
        ++wire_faults_;
        return;
    }
    ++acks_;
}

void MegaDriveCore::pump_(IPcmFeed& fd) noexcept {
    const PcmFeedState s = fd.state();
    if (!s.playing || paused_)
        return;
    hal::ISpiTransport& link = host().link;
    PcmRingPoint read_point{};
    {
        hal::Selected cs(link, hal::ChipSelect::Io);
        const auto rd = link.transfer(hal::SpiWord{kCmdAudio});
        if (!rd) {
            ++wire_faults_;
            return;
        }
        read_point = PcmRingPoint::of(*rd);
        const auto w = link.transfer(s.write_point.wire_word());
        if (!w) {
            ++wire_faults_;
            return;
        }
    }
    ++exchanges_;

    fd.observe_read_point(read_point);
}

void MegaDriveCore::answer_no_play_() noexcept {
    current_track_.reset();
    send_ack_(std::nullopt, false);
}

void MegaDriveCore::hold_play_() noexcept {

    if (!sheet_.waited_past())
        return;
    paused_ = false;
    ++play_refusals_;
    answer_no_play_();
}

void MegaDriveCore::arm_play_(IPcmFeed& fd, svc::TrackNumber track, bool loop) noexcept {
    paused_ = false;

    if (!fd.can_serve()) {
        play_.arm(mint_(), detail::PlayAct{.track = track, .loop = loop});
        play_.declined();
        ++play_refusals_;
        answer_no_play_();
        return;
    }

    const detail::PlayAct ask{.track = track, .loop = loop, .served_before = fd.state().served};
    play_.arm(mint_(), ask);
    if (auto r = fd.play(track, loop); !r) {
        play_.declined();
        ++play_refusals_;
        answer_no_play_();
    }
}

void MegaDriveCore::service_pcm_tick() noexcept {
    TASTY_SEAT_BODY(MegaDriveCore);
    const infra::OptRef<IPcmFeed> fdp = feed();
    if (!fdp)
        return;
    IPcmFeed& fd = *fdp;
    retry_feed_(fd);

    const std::optional<Status> st = read_status_();
    if (!st)
        return;

    if (play_.owed()) {
        const PcmFeedState s = fd.state();
        if (!s.primed || s.served == play_.act().served_before) {
            if (!play_.waited_past())
                return;
            ++ack_timeouts_;
            play_.stranded();
            answer_no_play_();
            return;
        }
        current_track_ = s.playing ? std::optional<svc::TrackNumber>{play_.act().track}
                                   : std::nullopt;
        play_.answered();
        send_ack_(current_track_, s.playing);
        pump_(fd);
        return;
    }
    if (st->flags.none()) {
        pump_(fd);
        return;
    }

    if (st->flags.has(md::CmdFlags::Play)) {
        if (!st->track) {

            send_ack_(current_track_, fd.state().playing);
            return;
        }

        if (sheet_.owed()) {
            hold_play_();
            return;
        }
        arm_play_(fd, *st->track, st->flags.has(md::CmdFlags::Loop));
        return;
    }
    if (st->flags.has(md::CmdFlags::Stop)) {

        if (st->fade == 0) {
            paused_ = true;
            if (auto r = fd.stop(); !r)
                paused_ = false;
        }
    } else if (st->flags.has(md::CmdFlags::Resume)) {
        paused_ = false;
        (void)fd.resume();
    }
    send_ack_(current_track_, fd.state().playing);
    pump_(fd);
}

}
