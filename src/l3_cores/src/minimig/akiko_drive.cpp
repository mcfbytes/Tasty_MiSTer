// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig/akiko_drive.h"

#include <algorithm>
#include <vector>

#include "cores/minimig/cd_bridge.h"
#include "cores/minimig/cd_bus.h"
#include "cores/minimig/cd_saves.h"
#include "infra/affine.h"
#include "infra/message_sum.h"
#include "infra/span_cut.h"
#include "svc/disc/subcode_interleaver.h"
#include "svc/msf.h"
#include "svc/read_bounded.h"

namespace mister::cores::minimig {

namespace {

using proto::Lba;
using svc::disc::kRedBookPregap;

constexpr infra::ByteOffset kInfoFirmwareAt{2};
constexpr infra::ByteOffset kMultiModeAt{7};
constexpr std::byte kDataRead{0x80};
constexpr std::byte kLedStrobe{0x80};

constexpr infra::ByteOffset kCascadeFrom{25};

constexpr proto::BlockCount kSubqEndSnap{10};

Lba msf_frames(std::span<const std::byte, 3> bytes) noexcept {
    constexpr std::size_t kFrameAt = 2;
    return svc::Msf{
        std::to_integer<std::uint8_t>(bytes[0]),
        std::to_integer<std::uint8_t>(bytes[1]),
        std::to_integer<std::uint8_t>(bytes[kFrameAt])
    }
        .lba_of_bcd();
}

bool is_none(const proto::MailboxAct& act) noexcept {
    return infra::kind_of(act) == proto::MailboxAct::Kind::None;
}

std::optional<std::vector<std::byte>> read_exact(
    const svc::Vfs& vfs,
    std::string_view path,
    std::size_t bytes
) {
    auto got = svc::read_bounded(vfs, path, bytes + 1);
    if (!got || got->size() != bytes)
        return std::nullopt;
    return std::move(*got);
}

}

void AkikoDrive::init() noexcept {
    TASTY_SEAT_BODY(AkikoDrive);
    initialized_ = akiko::Handshake::Fresh;
    paused_ = false;
    playing_ = false;
    led_on_ = false;
    play_start_ = Lba{};
    data_base_.reset();
    audio_timeout_ = {};
    disarm_cdda_();
    media_ = Media::Unknown;
    media_push_pending_ = false;
    media_event_ = false;
    swap_absent_ = false;
    swap_ready_.reset();
    toc_.clear();
    prefetch_.invalidate();
    commands_.clear();
    reply_owed_.reset();
    subcode_owed_.reset();
    counter_.reset();
    save_dirty_observed_ = false;
    save_load_failed_ = false;
    if (!save_path_.empty())
        save_load_pending_ = true;
}

void AkikoDrive::forget() noexcept {
    TASTY_SEAT_BODY(AkikoDrive);
    active_ = false;
    disc_.reset();
    save_path_ = {};
    flush_to_.reset();
    verify_due_.reset();
    save_load_pending_ = false;
    pending_ = Pending::None;
    init();
}

void AkikoDrive::bind(std::optional<CdDisc> disc, std::string_view path) noexcept {
    TASTY_SEAT_BODY(AkikoDrive);
    media_event_ = true;
    disc_ = std::move(disc);
    const SavePath next = cd_save_path(CdSave::Cd32Nvram, disc_ ? path : std::string_view{});
    if (next == save_path_)
        return;

    if (!save_path_.empty() && save_dirty_observed_)
        flush_to_ = save_path_;
    save_path_ = next;
    save_dirty_observed_ = false;
    save_load_failed_ = false;
    save_load_pending_ = !save_path_.empty();
}

void AkikoDrive::on_command(std::span<const std::byte> bytes) noexcept {
    TASTY_SEAT_BODY(AkikoDrive);
    if (bytes.empty() || commands_.full())
        return;
    Command c{};
    c.len = infra::ByteCount{std::min(bytes.size(), akiko::kFrameMax)};
    std::ranges::copy(infra::affine::first(bytes, c.len), c.bytes.begin());
    commands_.push(c);
}

void AkikoDrive::on_sector_counter(akiko::SectorCounter counter) noexcept {
    TASTY_SEAT_BODY(AkikoDrive);
    counter_ = counter;
}

void AkikoDrive::on_osd(bool shown) noexcept {
    TASTY_SEAT_BODY(AkikoDrive);
    if (shown && !osd_was_shown_ && status_.akiko_nvram_dirty())
        osd_edge_ = true;
    osd_was_shown_ = shown;
}

void AkikoDrive::settled(
    const proto::MailboxResult& result,
    std::span<const std::byte> bulk
) noexcept {
    TASTY_SEAT_BODY(AkikoDrive);
    const Pending was = std::exchange(pending_, Pending::None);
    if (was == Pending::NvramDump && result.end == proto::MailboxResult::End::Done)
        dumped_(bulk.first(kNvramBytes));
}

std::span<const Track> AkikoDrive::tracks_() const noexcept {
    return disc_ ? disc_->rows() : std::span<const Track>{};
}

svc::TrackCount AkikoDrive::real_tracks_() const noexcept {
    return infra::affine::saturating_sub(
        svc::TrackCount::truncating(tracks_().size()),
        svc::TrackCount{1}
    );
}

void AkikoDrive::disarm_cdda_() noexcept {
    cdda_play_.reset();
}

proto::MailboxAct AkikoDrive::written_(proto::MailboxAct act) noexcept {
    if (!is_none(act))
        pending_ = Pending::Write;
    return act;
}

void AkikoDrive::media_edge_() noexcept {
    const Media level = mounted_() ? Media::Present : Media::Absent;
    if (level == media_ && !media_event_)
        return;
    const bool swap = media_event_
        && level == Media::Present
        && initialized_ == akiko::Handshake::Identified;
    media_event_ = false;
    data_base_.reset();
    prefetch_.invalidate();
    disarm_cdda_();
    toc_.clear();
    media_ = level;
    media_push_pending_ = true;
    if (swap)
        swap_absent_ = true;
    if (present_()) {
        toc_.build(tracks_());
        toc_last_.reset();
    }
}

proto::MailboxAct AkikoDrive::next(Instant now, BulkBytes bulk) noexcept {
    TASTY_SEAT_BODY(AkikoDrive);
    media_edge_();
    if (!active_)
        return {};

    if (subcode_owed_) {
        const akiko::Subcode sub = *std::exchange(subcode_owed_, std::nullopt);
        return written_(cd_bus::write_bytes(cd_bridge::kAkikoSubcode, sub, bulk));
    }
    if (proto::MailboxAct a = saves_(now, bulk); !is_none(a))
        return a;
    if (proto::MailboxAct a = pushes_(now, bulk); !is_none(a))
        return a;

    if (status_.akiko_nvram_dirty())
        save_dirty_observed_ = true;
    if (std::exchange(osd_edge_, false) && !(save_load_failed_ && !save_path_.empty()))
        return dump_(
            Dump::Save,
            save_path_.empty() ? cd32_shared_save_path().view() : save_path_.view()
        );
    if (audio_timeout_.armed() && !status_.akiko_rx_busy()) {
        if (const std::optional<akiko::PlayEnd> end = audio_timeout_.step()) {
            playend_(*end);
            if (akiko::stops_play(*end))
                playing_ = false;
        }
        return reply_act_(bulk);
    }
    if (proto::MailboxAct a = cdda_(bulk); !is_none(a))
        return a;
    if (status_.akiko_sector_request()) {
        if (proto::MailboxAct a = sector_(bulk); !is_none(a))
            return a;
    }
    return command_(bulk);
}

proto::MailboxAct AkikoDrive::reply_act_(BulkBytes bulk) noexcept {
    if (!reply_owed_)
        return {};
    const Command r = *std::exchange(reply_owed_, std::nullopt);
    return written_(
        cd_bus::write_bytes(
            cd_bridge::kAkikoCommand,
            infra::affine::first(std::span(r.bytes), r.len),
            bulk
        )
    );
}

std::optional<AkikoDrive::Instant> AkikoDrive::deadline(Instant now) const noexcept {
    if (!active_)
        return std::nullopt;
    std::optional<Instant> at{};
    const auto sooner = [&at](Instant t) {
        at = at ? std::min(*at, t) : t;
    };
    if (swap_ready_ && media_push_pending_)
        sooner(*swap_ready_);
    if (verify_due_)
        sooner(*verify_due_);
    if (initialized_ == akiko::Handshake::Identified && toc_.dripping())
        sooner(toc_last_ ? *toc_last_ + kTocPeriod : now);

    if (audio_timeout_.armed() || (counter_ && status_.akiko_sector_request()))
        sooner(now + kPass);
    return at;
}

proto::MailboxAct AkikoDrive::saves_(Instant now, BulkBytes bulk) noexcept {
    if (flush_to_) {
        const SavePath to = *std::exchange(flush_to_, std::nullopt);
        return dump_(Dump::Flush, to.view());
    }

    if (save_load_pending_ && mounted_()) {
        save_load_pending_ = false;
        if (!save_path_.empty()) {
            const SavePath shared = cd32_shared_save_path();
            const bool migrate = !vfs_.file_exists(save_path_.view())
                && read_exact(vfs_, shared.view(), kNvramBytes).has_value();
            const proto::MailboxAct a = load_nvram_(
                migrate ? shared.view() : save_path_.view(),
                now,
                bulk
            );
            if (!is_none(a))
                return a;
        }
    }
    if (verify_due_ && now >= *verify_due_) {
        verify_due_.reset();
        return dump_(Dump::Verify, verify_path_.view());
    }
    return {};
}

proto::MailboxAct AkikoDrive::pushes_(Instant now, BulkBytes bulk) noexcept {
    if (status_.akiko_rx_busy())
        return {};
    if (initialized_ == akiko::Handshake::Fresh) {
        initialized_ = akiko::Handshake::Greeted;
        if (present_())
            reply_(
                std::array<std::byte, 2>{
                    akiko::head_byte(akiko::Notify::MediaStatus),
                    akiko::kMediaPresent
                }
            );
    } else if (swap_absent_ && initialized_ == akiko::Handshake::Identified) {

        reply_(
            std::array<std::byte, 2>{
                akiko::head_byte(akiko::Notify::MediaStatus),
                akiko::kMediaAbsent
            }
        );
        swap_absent_ = false;
        swap_ready_ = now + kSwapSettle;
    } else if (
        media_push_pending_
        && initialized_ == akiko::Handshake::Identified
        && (!swap_ready_ || now >= *swap_ready_)
    ) {
        swap_ready_.reset();
        reply_(
            std::array<std::byte, 2>{
                akiko::head_byte(akiko::Notify::MediaStatus),
                present_() ? akiko::kMediaPresent : akiko::kMediaAbsent
            }
        );
        media_push_pending_ = false;
        toc_.build(tracks_());
        toc_last_.reset();
    } else if (
        initialized_ == akiko::Handshake::Identified
        && toc_.dripping()
        && (!toc_last_ || now - *toc_last_ >= kTocPeriod)
    ) {
        toc_last_ = now;
        if (const std::optional<akiko::SubqReply> e = toc_.next())
            reply_(e->bytes());
    }
    return reply_act_(bulk);
}

proto::MailboxAct AkikoDrive::cdda_(BulkBytes bulk) noexcept {
    if (!cdda_play_ || paused_)
        return {};
    if (!disc_) {
        disarm_cdda_();
        audio_timeout_ = akiko::AudioTimeout::kFailed;
        return {};
    }
    if (!status_.cdda_request())
        return {};
    std::array<std::byte, svc::disc::kRawFrameBytes> buf{};
    const Lba lba = cdda_play_->next;
    const bool read = disc_->read_audio(lba, buf, CdDisc::Gap::Previous);
    pending_ = Pending::Audio;
    if (!read) {
        buf.fill(std::byte{});
        disarm_cdda_();
        audio_timeout_ = akiko::AudioTimeout::kFailed;
        return cd_bus::write_swapped(cd_bridge::kCdda, buf, bulk);
    }
    cdda_play_->next = lba + proto::BlockCount{1};
    cdda_q_pos_ = lba;
    akiko::Subcode sub = akiko::subcode_channels(tracks_(), lba);
    svc::disc::SubcodeInterleaver{}.shape(sub);
    subcode_owed_ = sub;
    if (cdda_play_->next >= cdda_play_->end) {
        cdda_q_pos_ = cdda_q_end_;
        disarm_cdda_();
        audio_timeout_ = akiko::AudioTimeout::kEnded;
    }
    return cd_bus::write_swapped(cd_bridge::kCdda, buf, bulk);
}

proto::MailboxAct AkikoDrive::sector_(BulkBytes bulk) noexcept {
    if (!counter_ || !data_base_ || !disc_)
        return {};
    const Lba lba = *data_base_ + counter_->offset();
    std::array<std::byte, svc::disc::kRawFrameBytes> buf{};
    const svc::TrackCount real = real_tracks_();
    const Lba lead_out = real > svc::TrackCount{}
        ? infra::at_index(tracks_(), real.next_index()).start
        : Lba{};
    const auto push = [&]() {
        counter_.reset();
        return written_(cd_bus::write_words(cd_bridge::kAkikoSector, buf, bulk));
    };
    if (lead_out == Lba{} || lba >= lead_out)
        return push();
    for (svc::TrackIndex i{}; real.contains(i); i += svc::TrackCount{1}) {
        const Track& t = infra::at_index(tracks_(), i);
        if (!CdDisc::holds(t, lba))
            continue;
        if (CdDisc::data(t))
            break;

        if (held_audio_.hit(lba, akiko::kAudioHold)) {
            data_base_.reset();
            playend_(akiko::PlayEnd::Ended);
            return reply_act_(bulk);
        }
        return {};
    }
    if (prefetch_.get(*disc_, lba, buf) != akiko::AkikoPrefetch::Got::Sector) {
        if (!read_retry_.hit(lba, akiko::kReadRetry))
            return {};
        buf.fill(std::byte{});
    }
    return push();
}

proto::MailboxAct AkikoDrive::command_(BulkBytes bulk) noexcept {
    const std::optional<Command> c = commands_.pop();
    if (!c)
        return {};
    const std::span<const std::byte> bytes = infra::affine::first(std::span(c->bytes), c->len);
    const akiko::CommandByte head{std::to_integer<std::uint8_t>(bytes.front())};
    if (const std::optional<infra::ByteCount> need = akiko::frame_bytes(head.opcode())) {

        if (c->len < *need
            || !akiko::FrameSum::of(infra::affine::first_clamped(bytes, *need)).holds())
            cmd_bad_(*c, akiko::DriveStatus::kChecksum);
        else
            dispatch_(*c);
    } else {
        dispatch_(*c);
    }
    return reply_act_(bulk);
}

void AkikoDrive::reply_(std::span<const std::byte> payload) noexcept {
    Command r{};
    r.len = akiko::framed(payload, r.bytes);
    if (!r.len.empty())
        reply_owed_ = r;
}

void AkikoDrive::dispatch_(const Command& c) noexcept {
    using akiko::Opcode;
    const akiko::CommandByte head{std::to_integer<std::uint8_t>(c.bytes[0])};
    switch (head.opcode()) {
        case Opcode::Nop:
            reply_(std::array<std::byte, 1>{head.wire_byte()});
            return;
        case Opcode::Stop:
            cmd_stop_(c);
            return;
        case Opcode::Pause:
            cmd_pause_(c, true);
            return;
        case Opcode::Unpause:
            cmd_pause_(c, false);
            return;
        case Opcode::Multi:
            cmd_multi_(c);
            return;
        case Opcode::Led:
            cmd_led_(c);
            return;
        case Opcode::Subq:
            cmd_subq_(c);
            return;
        case Opcode::Info:
            cmd_info_(c);
            return;
        case Opcode::Silent8:
        case Opcode::Silent9:
        case Opcode::SilentA:
            return;
        case Opcode::BadB:
        case Opcode::BadC:
        case Opcode::BadD:
        case Opcode::BadE:
        case Opcode::BadF:
            cmd_bad_(c, akiko::DriveStatus::kBadCommand);
            return;
    }
}

void AkikoDrive::cmd_info_(const Command& c) noexcept {
    std::array<std::byte, 20> r{};
    r[0] = c.bytes[0];
    r[1] = akiko::DriveStatus::kDoorClosed.wire_byte();
    std::ranges::transform(
        akiko::kFirmware,
        infra::affine::from(std::span(r), kInfoFirmwareAt).begin(),
        [](char ch) { return std::byte{static_cast<std::uint8_t>(ch)}; }
    );
    reply_(r);
    initialized_ = akiko::Handshake::Identified;
    media_push_pending_ = true;
    toc_.build(tracks_());
    toc_last_.reset();
}

void AkikoDrive::playend_(akiko::PlayEnd state) noexcept {
    const akiko::DriveStatus status = state == akiko::PlayEnd::Error ? akiko::DriveStatus::kError
        : state == akiko::PlayEnd::Playing
        ? akiko::DriveStatus::kPlaying.with(akiko::DriveStatus::kPlayStarted)
        : akiko::DriveStatus::kPlayEnd;
    const std::array<std::byte, 2> r{
        akiko::head_byte(akiko::Notify::PlayEnd),
        status.with(akiko::DriveStatus::kDoorClosed).wire_byte()
    };
    reply_(r);
}

void AkikoDrive::cmd_stop_(const Command& c) noexcept {
    const std::array<std::byte, 2> r{
        c.bytes[0],
        present_() ? akiko::DriveStatus::kPlayEnd.wire_byte()
                   : akiko::DriveStatus::kNoDisk.with(akiko::DriveStatus::kDoorClosed).wire_byte()
    };
    playing_ = false;
    paused_ = false;
    data_base_.reset();
    disarm_cdda_();
    audio_timeout_.cancel_countdown();
    reply_(r);
}

void AkikoDrive::cmd_pause_(const Command& c, bool pause) noexcept {
    const akiko::DriveStatus status = present_()
        ? (playing_ ? akiko::DriveStatus::kPlaying : akiko::DriveStatus::kPlayEnd)
              .with(akiko::DriveStatus::kDoorClosed)
        : akiko::DriveStatus::kNoDisk.with(akiko::DriveStatus::kDoorClosed);
    const std::array<std::byte, 2> r{c.bytes[0], status.wire_byte()};
    if (pause) {
        toc_.disarm();
        toc_last_.reset();
    }
    paused_ = pause;
    reply_(r);
}

void AkikoDrive::cmd_multi_(const Command& c) noexcept {
    std::array<std::byte, 2> r{c.bytes[0], akiko::DriveStatus::kPlayEnd.wire_byte()};
    if (!present_()) {
        r[1] = akiko::DriveStatus::kDoorClosed.wire_byte();
        reply_(r);
        return;
    }
    const std::span<const std::byte> args = std::span(c.bytes).subspan(1);
    const Lba s_msf = msf_frames(args.first<3>());
    const bool seek_negative = s_msf < Lba{} + kRedBookPregap;
    const Lba s_lba = s_msf - kRedBookPregap;
    const Lba e_lba = msf_frames(args.subspan<3, 3>()) - kRedBookPregap;
    play_start_ = s_lba;
    const std::span<const Track> t = tracks_();
    const svc::TrackCount real = real_tracks_();
    const std::byte mode = infra::affine::from(std::span(c.bytes), kMultiModeAt).front();
    if ((mode & kDataRead) != std::byte{}) {

        bool audio = false;
        bool past = false;
        if (!seek_negative && disc_) {
            const Lba lead_out = real > svc::TrackCount{}
                ? infra::at_index(t, real.next_index()).start
                : Lba{};
            past = lead_out != Lba{} && s_lba >= lead_out;
            for (svc::TrackIndex i{}; real.contains(i); i += svc::TrackCount{1}) {
                if (past)
                    break;
                const Track& row = infra::at_index(t, i);
                if (CdDisc::holds(row, s_lba)) {
                    audio = !CdDisc::data(row);
                    break;
                }
                if (real.contains(i + svc::TrackCount{1})
                    && s_lba >= row.end
                    && s_lba < infra::at_index(t, i + svc::TrackCount{1}).start) {
                    audio = !CdDisc::data(infra::at_index(t, i + svc::TrackCount{1}));
                    break;
                }
            }
        }
        if (audio) {
            data_base_.reset();
            r[1] = akiko::DriveStatus::kPlayEnd.with(akiko::DriveStatus::kDoorClosed).wire_byte();
        } else if (past) {
            data_base_.reset();
            r[1] = akiko::DriveStatus::kPlayStarted.wire_byte();
        } else {
            data_base_ = s_lba;
            paused_ = false;
            r[1] = akiko::DriveStatus::kPlayStarted.wire_byte();
        }
    } else if (seek_negative) {

        data_base_.reset();
        playing_ = false;
        paused_ = false;
        toc_.arm();
        toc_last_.reset();
        r[1] = akiko::DriveStatus::kPlayEnd.wire_byte();
    } else {
        data_base_.reset();
        playing_ = true;
        paused_ = false;
        r[1] = akiko::DriveStatus::kPlayCommandStarting.wire_byte();
        audio_timeout_ = akiko::AudioTimeout::countdown(akiko::kPlayStartPasses);
        bool valid = false;
        if (e_lba > s_lba) {
            for (svc::TrackIndex i{}; real.contains(i); i += svc::TrackCount{1}) {
                if (CdDisc::holds(infra::at_index(t, i), s_lba)) {
                    valid = !CdDisc::data(infra::at_index(t, i));
                    break;
                }
            }
        }
        if (valid) {

            const bool same = cdda_play_
                && cdda_play_->end == e_lba
                && cdda_play_->next >= s_lba
                && cdda_play_->next < e_lba;
            if (!same) {
                cdda_play_ = CddaPlay{.next = s_lba, .end = e_lba};
                cdda_q_pos_ = s_lba;
            }
            cdda_q_end_ = e_lba;
        } else {
            disarm_cdda_();
            audio_timeout_ = akiko::AudioTimeout::kFailed;
            playing_ = false;
        }
    }
    reply_(r);
}

void AkikoDrive::cmd_led_(const Command& c) noexcept {
    const std::byte byte = c.bytes[1];
    if ((byte & kLedStrobe) == std::byte{})
        return;
    led_on_ = (std::to_integer<unsigned>(byte) & 1u) != 0;
    const std::array<std::byte, 2> r{c.bytes[0], led_on_ ? std::byte{1} : std::byte{}};
    reply_(r);
}

void AkikoDrive::cmd_subq_(const Command& c) noexcept {
    const akiko::CommandByte head{std::to_integer<std::uint8_t>(c.bytes[0])};
    std::optional<Lba> cur{};
    if (disc_) {
        if (data_base_) {
            cur = *data_base_ + (counter_ ? counter_->offset() : proto::BlockCount{});
        } else if (playing_ && cdda_q_pos_) {
            Lba at = *cdda_q_pos_;
            const bool near_end = cdda_q_end_
                && *cdda_q_end_ > Lba{}
                && (at > *cdda_q_end_ || *cdda_q_end_ - at < kSubqEndSnap);
            if (near_end)
                at = *cdda_q_end_;
            cur = at;
        } else if (playing_ && play_start_ > Lba{}) {
            cur = play_start_;
        }
    }
    if (!cur) {
        reply_(akiko::SubqReply::unknown(head).bytes());
        return;
    }
    const std::span<const Track> t = tracks_();
    const svc::TrackCount real = real_tracks_();
    svc::TrackIndex idx{};
    for (svc::TrackIndex i{}; real.contains(i); i += svc::TrackCount{1}) {
        const Track& row = infra::at_index(t, i);
        const Lba end = infra::at_index(t, i + svc::TrackCount{1}).start;
        if (*cur >= row.start && *cur < end) {
            idx = i;
            break;
        }
    }
    const Track& tr = infra::at_index(t, idx);
    const proto::BlockCount into = *cur >= tr.start ? *cur - tr.start : proto::BlockCount{};
    const svc::disc::QChannel q = svc::disc::QChannel::of_track(
        CdDisc::q_control(tr),
        tr.number,
        svc::disc::QTimes{
            svc::Msf::of_lba_bcd_masked(Lba{} + into + kRedBookPregap),
            svc::Msf::of_lba_bcd_masked(*cur + kRedBookPregap)
        }
    );
    reply_(akiko::SubqReply::of(head, q).bytes());
}

void AkikoDrive::cmd_bad_(const Command& c, akiko::DriveStatus err) noexcept {
    const akiko::CommandByte head{std::to_integer<std::uint8_t>(c.bytes[0])};
    const std::array<std::byte, 2> r{
        head.error_echo().wire_byte(),
        err.with(akiko::DriveStatus::kDoorClosed).wire_byte()
    };
    reply_(r);
}

proto::MailboxAct AkikoDrive::load_nvram_(
    std::string_view path,
    Instant now,
    BulkBytes bulk
) noexcept {
    if (!vfs_.file_exists(path))
        return {};
    const std::optional<std::vector<std::byte>> image = read_exact(vfs_, path, kNvramBytes);
    if (!image) {

        auto any = svc::read_bounded(vfs_, path, kNvramBytes + 1);
        if (!any)
            save_load_failed_ = true;
        return {};
    }
    save_load_failed_ = false;
    (void)verify_path_.assign(path);
    verify_due_ = now + kVerifyAfter;
    return written_(cd_bus::write_words(cd_bridge::kAkikoNvram, *image, bulk));
}

proto::MailboxAct AkikoDrive::dump_(Dump why, std::string_view path) noexcept {
    dump_for_ = why;
    (void)dump_path_.assign(path);
    pending_ = Pending::NvramDump;
    return cd_bus::read_bytes(cd_bridge::kAkikoNvram, infra::ByteCount{kNvramBytes});
}

void AkikoDrive::dumped_(std::span<const std::byte> image) noexcept {
    switch (dump_for_) {
        case Dump::Verify: {
            const std::optional<std::vector<std::byte>> expected = read_exact(
                vfs_,
                dump_path_.view(),
                kNvramBytes
            );
            save_load_failed_ = !expected || !std::ranges::equal(*expected, image);
            return;
        }
        case Dump::Save:
        case Dump::Flush:
            (void)save_(dump_path_.view(), image);
            return;
    }
}

bool AkikoDrive::save_(std::string_view path, std::span<const std::byte> image) noexcept {
    if (const std::optional<std::vector<std::byte>> disk = read_exact(vfs_, path, kNvramBytes)) {
        if (std::ranges::equal(*disk, image))
            return true;
        const auto entries = [](std::span<const std::byte> b) {
            return std::ranges::count_if(infra::affine::from(b, kCascadeFrom), [](std::byte x) {
                return x != std::byte{};
            });
        };
        if (entries(image) == 0 && entries(*disk) > 0)
            return false;
    }
    return writer_.replace_file(path, image).has_value();
}

}
