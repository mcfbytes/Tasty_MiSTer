// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig/cr511_drive.h"

#include <algorithm>

#include "cores/minimig/cd_bridge.h"
#include "cores/minimig/cd_bus.h"
#include "cores/minimig/cd_saves.h"
#include "infra/affine.h"
#include "infra/message_sum.h"
#include "infra/span_cut.h"

namespace mister::cores::minimig {

namespace {

using proto::Lba;
using svc::disc::kRedBookPregap;
using Address = cr511::Address;
using AddressForm = cr511::AddressForm;
using AddressPair = cr511::Address::Pair;
using Op = cr511::Op;

constexpr infra::ByteCount kBlankLong{14};
constexpr infra::ByteCount kBlankShort{4};

static_assert(
    proto::MailboxBusLen::widened(infra::ByteCount{Cr511Drive::kSectorPieceBytes}).fits()
);

bool is_none(const proto::MailboxAct& act) noexcept {
    return infra::kind_of(act) == proto::MailboxAct::Kind::None;
}

}

void Cr511Drive::init(Instant now) noexcept {
    TASTY_SEAT_BODY(Cr511Drive);
    framer_ = {};
    commands_.clear();
    read_.reset();
    motor_ = false;
    media_ = !path_.empty();
    isready_ = false;
    playing_ = false;
    paused_ = false;
    audio_status_ = cr511::AudioStatus::NoStatus;
    last_lba_ = Lba{};
    play_.reset();
    finished_ = media_;
    error_ = false;
    sector_size_ = cr511::kUserData;
    if (media_) {
        stch_retries_ = StchBudget::kMediaChange;
        stch_next_ = now;
    }
    swap_pending_ = false;
    swap_ready_.reset();
}

void Cr511Drive::forget(Instant now) noexcept {
    TASTY_SEAT_BODY(Cr511Drive);
    active_ = false;
    disc_.reset();
    path_.clear();
    nvram_path_ = {};
    card_path_ = {};
    stch_retries_ = StchBudget::none();
    stch_wait_ack_ = false;
    stch_playstart_ = false;
    pending_ = Pending::None;
    init(now);
}

void Cr511Drive::bind(std::optional<CdDisc> disc, std::string_view path, Instant now) noexcept {
    TASTY_SEAT_BODY(Cr511Drive);
    disc_ = std::move(disc);
    if (!disc_)
        path = {};
    card_path_ = cd_save_path(CdSave::CdtvCard, path);
    nvram_path_ = cd_save_path(CdSave::CdtvNvram, path);
    const bool has_path = !path.empty();
    const bool swap = media_ && has_path && std::string_view{path_} != path;
    path_.assign(path);
    media_ = !swap && has_path;
    isready_ = false;
    motor_ = false;
    playing_ = false;
    paused_ = false;
    audio_status_ = cr511::AudioStatus::NoStatus;
    last_lba_ = Lba{};
    play_.reset();
    finished_ = has_path;
    stch_retries_ = StchBudget::kMediaChange;
    stch_next_ = now;
    swap_pending_ = swap;
    swap_ready_ = swap ? std::optional<Instant>{now + kSwapSettle} : std::nullopt;
}

void Cr511Drive::on_status(CdStatus status) noexcept {
    TASTY_SEAT_BODY(Cr511Drive);
    status_ = status;
}

void Cr511Drive::on_byte(std::byte byte) noexcept {
    TASTY_SEAT_BODY(Cr511Drive);
    if (const std::optional<CommandFrame> frame = framer_.push(byte))
        if (!commands_.full())
            commands_.push(*frame);
}

void Cr511Drive::settled(
    const proto::MailboxResult& result,
    std::span<const std::byte> bulk
) noexcept {
    TASTY_SEAT_BODY(Cr511Drive);
    const Pending was = std::exchange(pending_, Pending::None);
    if (result.end != proto::MailboxResult::End::Done)
        return;
    switch (was) {
        case Pending::StchAck:
            if (stch_wait_ack_ && !bulk.empty() && (std::to_integer<unsigned>(bulk[0]) & 1u) != 0) {
                stch_wait_ack_ = false;
                stch_retries_ = StchBudget::none();
            }
            return;
        case Pending::None:
        case Pending::Write:
            return;
    }
}

std::span<const Track> Cr511Drive::tracks_() const noexcept {
    return disc_ ? disc_->rows() : std::span<const Track>{};
}

svc::TrackCount Cr511Drive::real_tracks_() const noexcept {
    return infra::affine::saturating_sub(
               svc::TrackCount::truncating(tracks_().size()),
               svc::TrackCount{1}
    )
        .at_least_one();
}

Lba Cr511Drive::disc_end_() const noexcept {
    Lba end{};
    for (const Track& t : tracks_())
        end = std::max(end, t.end);
    return end;
}

proto::MailboxAct Cr511Drive::written_(proto::MailboxAct act) noexcept {
    if (!is_none(act))
        pending_ = Pending::Write;
    return act;
}

proto::MailboxAct Cr511Drive::inject_stch_(BulkBytes bulk) noexcept {
    return written_(
        cd_bus::write_bytes(cd_bridge::kCdtvStch, std::array<std::byte, 1>{std::byte{}}, bulk)
    );
}

proto::MailboxAct Cr511Drive::next(Instant now, BulkBytes bulk) noexcept {
    TASTY_SEAT_BODY(Cr511Drive);
    if (!active_)
        return {};

    if (read_)
        return read_data_(now, bulk);
    if (swap_pending_ && swap_ready_ && now >= *swap_ready_) {
        swap_pending_ = false;
        media_ = true;
        finished_ = true;
        stch_retries_ = StchBudget::kMediaChange;
        stch_next_ = now;
    }
    if (proto::MailboxAct a = stch_(now, bulk); !is_none(a))
        return a;
    if (proto::MailboxAct a = cdda_(now, bulk); !is_none(a))
        return a;
    if (proto::MailboxAct a = command_(bulk); !is_none(a))
        return a;
    if (stch_wait_ack_) {
        pending_ = Pending::StchAck;
        return cd_bus::read_bytes(cd_bridge::kCdtvStch, infra::ByteCount{1});
    }
    return {};
}

std::optional<Cr511Drive::Instant> Cr511Drive::deadline(Instant now) const noexcept {
    if (!active_)
        return std::nullopt;
    std::optional<Instant> at{};
    const auto sooner = [&at](Instant t) {
        at = at ? std::min(*at, t) : t;
    };
    if (swap_pending_ && swap_ready_)
        sooner(*swap_ready_);
    if (stch_retries_.pending() && stch_next_)
        sooner(*stch_next_);
    if (stch_wait_ack_)
        sooner(now + kSectorGap);
    return at;
}

proto::MailboxAct Cr511Drive::stch_(Instant now, BulkBytes bulk) noexcept {
    if (std::exchange(stch_playstart_, false))
        return inject_stch_(bulk);
    if (stch_retries_.pending() && stch_next_ && now >= *stch_next_) {
        stch_retries_.spend();
        stch_next_ = now + (stch_wait_ack_ ? kPlayEndPeriod : kStchPeriod);
        return inject_stch_(bulk);
    }
    return {};
}

void Cr511Drive::play_end_(cr511::AudioStatus audio_status) noexcept {
    play_.reset();
    playing_ = false;
    paused_ = false;
    finished_ = true;
    audio_status_ = audio_status;
    stch_playstart_ = true;
}

proto::MailboxAct Cr511Drive::cdda_(Instant now, BulkBytes bulk) noexcept {
    if (!play_ || paused_)
        return {};
    if (!disc_) {
        play_.reset();
        return {};
    }
    if (!status_.cdda_request())
        return {};
    std::array<std::byte, svc::disc::kRawFrameBytes> buf{};
    const Lba lba = play_->next;
    last_lba_ = lba;
    if (!disc_->read_audio(lba, buf, CdDisc::Gap::Own)) {
        buf.fill(std::byte{});
        play_end_(cr511::AudioStatus::PlayError);
        return written_(cd_bus::write_swapped(cd_bridge::kCdda, buf, bulk));
    }
    play_->next = lba + proto::BlockCount{1};
    if (play_->next >= play_->end) {
        play_end_(cr511::AudioStatus::PlayComplete);
        stch_wait_ack_ = true;
        stch_retries_ = StchBudget::kPlayEnd;
        stch_next_ = now + kPlayEndPeriod;
    }
    return written_(cd_bus::write_swapped(cd_bridge::kCdda, buf, bulk));
}

bool Cr511Drive::stage_sector_(Read& r) noexcept {
    std::array<std::byte, svc::disc::kRawFrameBytes> raw{};
    if (!disc_ || !disc_->read(r.lba, raw))
        return false;
    const cr511::SectorSlice slice = cr511::slice_of(sector_size_);
    const infra::ByteCount n = std::min(
        slice.bytes,
        infra::ByteOffset{svc::disc::kRawFrameBytes} - slice.from
    );
    const std::span<const std::byte> piece = infra::affine::first(
        infra::affine::from(std::span(raw), slice.from),
        n
    );
    std::ranges::copy(piece, r.slice.begin());
    r.staged = n;
    r.sent = infra::ByteCount{};
    r.lba = r.lba + proto::BlockCount{1};
    r.left -= proto::BlockCount{1};
    return true;
}

proto::MailboxAct Cr511Drive::read_data_(Instant now, BulkBytes bulk) noexcept {
    Read& r = *read_;
    const bool fresh = r.sent == r.staged;
    if (fresh && r.left == proto::BlockCount{}) {
        finished_ = true;
        read_.reset();
        return {};
    }
    if (fresh && !stage_sector_(r)) {
        error_ = true;
        finished_ = true;
        read_.reset();
        return {};
    }
    const Instant after = fresh ? r.after.value_or(Instant{}) : Instant{};
    const infra::ByteCount piece = std::min(r.staged - r.sent, kSectorPieceBytes);
    const std::span<const std::byte> bytes = infra::affine::first(
        infra::affine::from(std::span(r.slice), r.sent),
        piece
    );
    r.sent += piece;
    if (r.sent == r.staged)
        r.after = now + kSectorGap;
    const proto::MailboxAct act = cd_bus::write_bytes(cd_bridge::kCdtvSector, bytes, bulk, after);
    if (r.sent == r.staged && r.left == proto::BlockCount{}) {
        finished_ = true;
        read_.reset();
    }
    return written_(act);
}

proto::MailboxAct Cr511Drive::command_(BulkBytes bulk) noexcept {
    const std::optional<CommandFrame> c = commands_.pop();
    if (!c)
        return {};
    const ReplyFrame out = dispatch_(*c);
    if (out.empty())
        return {};
    return written_(cd_bus::write_bytes(cd_bridge::kCdtvCommand, out.bytes(), bulk));
}

std::optional<Cr511Drive::CommandFrame> Cr511Drive::Framer::push(std::byte byte) noexcept {
    if (have.empty()) {
        const std::optional<cr511::Op> op = cr511::op_of(byte);
        if (!op)
            return std::nullopt;
        need = cr511::frame_bytes(*op);
    }
    if (have < kFrameCap) {
        partial.store(have, byte);
        have += infra::ByteCount{1};
    }
    if (have < need)
        return std::nullopt;
    const CommandFrame done = partial;
    partial = {};
    have = infra::ByteCount{};
    need = infra::ByteCount{};
    return done;
}

void Cr511Drive::stop_play_() noexcept {
    play_.reset();
    playing_ = false;
    paused_ = false;
    motor_ = false;
    audio_status_ = cr511::AudioStatus::NoStatus;
}

Cr511Drive::ReplyFrame Cr511Drive::dispatch_(const CommandFrame& c) noexcept {
    if (c.op() != Op::Status && stch_retries_.pending()) {
        stch_retries_ = StchBudget::none();
        stch_wait_ack_ = false;
    }
    switch (c.op()) {
        case Op::Sync:
        case Op::SyncAlt:
            return ReplyFrame::sync();
        case Op::Seek:
        case Op::FrontPanel:
            finished_ = true;
            return ReplyFrame::none();
        case Op::Read: {
            if (!disc_ || !disc_->chd()) {
                error_ = true;
                finished_ = true;
                return ReplyFrame::none();
            }
            read_ = Read{.lba = c.read_start(), .left = c.read_count()};
            if (read_->left == proto::BlockCount{}) {
                finished_ = true;
                read_.reset();
            }
            return ReplyFrame::none();
        }
        case Op::MotorOn:
            motor_ = true;
            finished_ = true;
            return ReplyFrame::none();
        case Op::MotorOff:
            stop_play_();
            finished_ = true;
            return ReplyFrame::none();
        case Op::PlayLsn:
        case Op::PlayMsf:
        case Op::PlayTrack:
            return cmd_play_(c);
        case Op::Status: {
            DriveFlags flags{};
            if (!isready_)
                flags = flags.with(DriveFlags::kNotReady);
            if (playing_)
                flags = flags.with(DriveFlags::kPlaying);
            if (finished_)
                flags = flags.with(DriveFlags::kFinished);
            if (error_)
                flags = flags.with(DriveFlags::kError);
            if (motor_)
                flags = flags.with(DriveFlags::kMotor);
            if (media_)
                flags = flags.with(DriveFlags::kMedia);
            finished_ = false;
            return ReplyFrame::status(flags);
        }
        case Op::ErrorInfo: {
            const bool was = error_;
            error_ = false;
            isready_ = false;
            finished_ = true;
            return ReplyFrame::error_info(was);
        }
        case Op::Model:
            finished_ = true;
            return ReplyFrame::model();
        case Op::ModeSet: {
            const svc::SectorSize size = c.mode_size();
            if (cr511::accepted_by_mode_set(size)) {
                sector_size_ = size;
                finished_ = true;
            } else {
                error_ = true;
            }
            return ReplyFrame::none();
        }
        case Op::ModeSense:
            return ReplyFrame::mode(sector_size_);
        case Op::Capacity:
            if (!has_tracks_()) {
                error_ = true;
                return ReplyFrame::none();
            }
            return ReplyFrame::capacity(
                Address::of_disc(disc_end_() - proto::BlockCount{1}, AddressForm::Lba),
                sector_size_
            );
        case Op::Subq:
            return cmd_subq_(c);
        case Op::Blank14:
            return ReplyFrame::blank(kBlankLong);
        case Op::DiscInfo:
            return cmd_info_();
        case Op::ReadToc:
            return cmd_read_toc_(c);
        case Op::Pause:
            paused_ = c.pause_requested();
            if (play_)
                audio_status_ = paused_ ? cr511::AudioStatus::Paused
                                        : cr511::AudioStatus::InProgress;
            finished_ = true;
            return ReplyFrame::none();
        case Op::Blank4:
            return ReplyFrame::blank(kBlankShort);
    }
    return ReplyFrame::none();
}

Cr511Drive::ReplyFrame Cr511Drive::cmd_play_(const CommandFrame& c) noexcept {
    if (!has_tracks_()) {
        error_ = true;
        return ReplyFrame::none();
    }
    const std::span<const Track> t = tracks_();
    const svc::TrackCount real = real_tracks_();
    const Lba lead_out = t.back().start;
    Lba s_lba{};
    Lba e_lba{};
    const auto stop = [&]() {
        stop_play_();
        error_ = true;
        return ReplyFrame::play(false);
    };
    if (c.op() == Op::PlayTrack) {
        const CommandFrame::PlayTracks tracks = c.play_tracks();
        if (tracks.from == svc::TrackNumber{} && tracks.to == svc::TrackNumber{})
            return stop();
        bool got = false;
        e_lba = lead_out;
        for (svc::TrackIndex i{}; real.contains(i); i += svc::TrackCount{1}) {
            if (i.number() == tracks.from) {
                s_lba = infra::at_index(t, i).start;
                got = true;
            }
            if (i.number() == tracks.to)
                e_lba = infra::at_index(t, i).start;
        }
        if (!got) {
            error_ = true;
            return ReplyFrame::play(false);
        }
    } else if (c.op() == Op::PlayLsn) {
        const CommandFrame::PlayLsn at = c.play_lsn();
        s_lba = at.from;
        e_lba = std::min(s_lba + at.count, lead_out);
    } else {
        const AddressPair at = c.play_msf();
        s_lba = at.first.msf_lba();
        e_lba = std::min(at.second.open_end() ? cr511::kNoEnd : at.second.msf_lba(), lead_out);
    }
    if (s_lba == Lba{} && e_lba == Lba{})
        return stop();
    bool audio = false;
    for (svc::TrackIndex i{}; real.contains(i); i += svc::TrackCount{1}) {
        if (CdDisc::holds(infra::at_index(t, i), s_lba)) {
            audio = !CdDisc::data(infra::at_index(t, i));
            break;
        }
    }
    if (audio && e_lba > s_lba) {
        play_ = Play{.next = s_lba, .end = e_lba};
        last_lba_ = s_lba;
        paused_ = false;
        audio_status_ = cr511::AudioStatus::InProgress;
    } else {
        play_.reset();
        audio_status_ = cr511::AudioStatus::PlayError;
    }
    playing_ = true;
    motor_ = true;
    stch_playstart_ = true;
    return ReplyFrame::play(true);
}

Cr511Drive::ReplyFrame Cr511Drive::cmd_subq_(const CommandFrame& c) const noexcept {
    const Lba disk = play_ ? play_->next : last_lba_;
    svc::TrackIndex idx{};
    Lba start{};
    const std::span<const Track> t = tracks_();
    if (t.size() > 1) {
        for (svc::TrackIndex i{}; real_tracks_().contains(i); i += svc::TrackCount{1}) {
            idx = i;
            start = infra::at_index(t, i).start;
            if (CdDisc::holds(infra::at_index(t, i), disk))
                break;
        }
    }
    const proto::BlockCount rel = disk > start ? disk - start : proto::BlockCount{};
    const AddressForm form = c.address_form();
    return ReplyFrame::subq(
        audio_status_,
        idx.number(),
        AddressPair{.first = Address::of_disc(disk, form), .second = Address::of_count(rel, form)}
    );
}

Cr511Drive::ReplyFrame Cr511Drive::cmd_read_toc_(const CommandFrame& c) noexcept {
    if (!has_tracks_()) {
        error_ = true;
        return ReplyFrame::none();
    }
    const std::span<const Track> t = tracks_();
    const svc::TrackCount real = real_tracks_();
    const AddressForm form = c.address_form();
    std::optional<Address> at{};
    if (const std::optional<svc::disc::TocPoint> descriptor = c.toc_descriptor()) {
        if (*descriptor == svc::disc::TocPoint::first_track_info())
            at = Address::of_a0(t.front().number, form);
        else if (*descriptor == svc::disc::TocPoint::last_track_info())
            at = Address::of_a1(real, form);
        else
            at = Address::of_disc(disc_end_(), form);
    } else if (
        const std::optional<svc::TrackNumber> track = c.toc_track();
        track && real.contains(track->index())
    ) {
        at = Address::of_disc(infra::at_index(t, track->index()).start, form);
    }
    if (!at)
        return ReplyFrame::none();
    finished_ = true;
    return ReplyFrame::toc(c.toc_byte(), real, *at);
}

Cr511Drive::ReplyFrame Cr511Drive::cmd_info_() noexcept {
    if (!has_tracks_()) {
        error_ = true;
        return ReplyFrame::none();
    }
    finished_ = true;
    return ReplyFrame::disc_info(real_tracks_(), Address::of_disc(disc_end_(), AddressForm::Msf));
}

Cr511Drive::ReplyFrame Cr511Drive::ReplyFrame::none() noexcept {
    return ReplyFrame{};
}

Cr511Drive::ReplyFrame Cr511Drive::ReplyFrame::sync() noexcept {
    ReplyFrame out;
    out.put(kFirstAt, kSyncFirst);
    out.put(kSecondAt, kSyncSecond);
    out.len = kSyncLen;
    return out;
}

Cr511Drive::ReplyFrame Cr511Drive::ReplyFrame::status(DriveFlags flags) noexcept {
    ReplyFrame out;
    out.put(kFirstAt, flags.wire_byte());
    out.len = kStatusLen;
    return out;
}

Cr511Drive::ReplyFrame Cr511Drive::ReplyFrame::error_info(bool error) noexcept {
    ReplyFrame out;
    if (error)
        out.put(kFlagAt, DriveFlags::kError.wire_byte());
    out.len = kErrorLen;
    return out;
}

Cr511Drive::ReplyFrame Cr511Drive::ReplyFrame::model() noexcept {
    ReplyFrame out;
    std::ranges::transform(
        cr511::kModel,
        infra::affine::from(std::span{out.data}, kFirstAt).begin(),
        [](char ch) { return std::byte{static_cast<std::uint8_t>(ch)}; }
    );
    out.len = infra::ByteCount{cr511::kModel.size()};
    return out;
}

Cr511Drive::ReplyFrame Cr511Drive::ReplyFrame::mode(svc::SectorSize size) noexcept {
    ReplyFrame out;
    out.put_size(kFirstAt, size);
    out.len = kModeLen;
    return out;
}

Cr511Drive::ReplyFrame Cr511Drive::ReplyFrame::capacity(
    Address end,
    svc::SectorSize size
) noexcept {
    ReplyFrame out;
    out.put_address(kFirstAt, end);
    out.put_size(kCapacitySizeAt, size);
    out.len = kCapacityLen;
    return out;
}

Cr511Drive::ReplyFrame Cr511Drive::ReplyFrame::subq(
    cr511::AudioStatus audio,
    svc::TrackNumber track,
    AddressPair at
) noexcept {
    ReplyFrame out;
    out.put(kFirstAt, cr511::status_byte(audio));
    out.put(kSecondAt, svc::disc::QControl::kAudio.adr_control_byte());
    out.put_track(kTrackAt, track);
    out.put(kIndexAt, kIndex01);

    out.put_address(kAddressAt, at.first);
    out.put_address(kRelativeAt, at.second);
    out.len = kSubqLen;
    return out;
}

Cr511Drive::ReplyFrame Cr511Drive::ReplyFrame::toc(
    std::byte point,
    svc::TrackCount real,
    Address at
) noexcept {
    ReplyFrame out;
    out.put(kFirstAt, std::byte{});
    out.put(kSecondAt, svc::disc::QControl::kData.adr_control_byte());
    out.put(kTrackAt, point);
    out.put_count(kIndexAt, real);
    out.put_address(kAddressAt, at);
    out.len = kTocLen;
    return out;
}

Cr511Drive::ReplyFrame Cr511Drive::ReplyFrame::disc_info(
    svc::TrackCount real,
    Address end
) noexcept {
    ReplyFrame out;
    out.put(kFirstAt, std::byte{1});
    out.put_count(kInfoCountAt, real);
    out.put_address(kInfoEndAt, end);
    out.len = kInfoLen;
    return out;
}

Cr511Drive::ReplyFrame Cr511Drive::ReplyFrame::play(bool accepted) noexcept {
    ReplyFrame out;
    out.put(kFirstAt, accepted ? kPlayAccepted : kPlayRefused);
    out.len = kPlayLen;
    return out;
}

Cr511Drive::ReplyFrame Cr511Drive::ReplyFrame::blank(infra::ByteCount n) noexcept {
    ReplyFrame out;
    out.len = n;
    return out;
}

}
