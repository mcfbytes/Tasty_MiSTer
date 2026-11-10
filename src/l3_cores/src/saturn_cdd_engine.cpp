// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/saturn_cdd_engine.h"

#include <array>
#include <cstring>
#include <optional>

#include "cd_core_detail.h"
#include "cores/saturn_seek.h"

namespace mister::cores {

namespace {

constexpr std::size_t kStatusWords = SaturnCddFrame::kBytes / 2;

constexpr std::uint8_t kCommNop = 0x00;
constexpr std::uint8_t kCommSeekRing = 0x02;
constexpr std::uint8_t kCommToc = 0x03;
constexpr std::uint8_t kCommStop = 0x04;
constexpr std::uint8_t kCommRead = 0x06;
constexpr std::uint8_t kCommPause = 0x08;
constexpr std::uint8_t kCommSeek = 0x09;

constexpr std::uint8_t kStatToc = 0x06;
constexpr std::uint8_t kStatStop = 0x12;
constexpr std::uint8_t kStatSeek = 0x22;
constexpr std::uint8_t kStatData = 0x36;
constexpr std::uint8_t kStatIdle = 0x46;
constexpr std::uint8_t kStatOpen = 0x80;
constexpr std::uint8_t kStatSeekRing = 0xB2;

constexpr std::uint8_t kAbsentTrack = 0xAA;
constexpr std::size_t kHeaderBytes = 16;
constexpr std::size_t kRingHeaderBytes = 12;

}

void SaturnCddEngine::reset_status_() noexcept {

    stat_.set(StatField::Status, kStatOpen);
    for (std::size_t i = 1; i <= 9; ++i)
        stat_.bytes[i] = 0;
    stat_.set(StatField::Checksum, 0);
    stat_.seal();
}

Ex<void> SaturnCddEngine::do_accept(std::span<const std::uint16_t> words, CdDialectPorts& p) {

    comm_.load_words(words);
    command_exec_(p.geom);
    return {};
}

void SaturnCddEngine::command_exec_(const svc::DiscGeometry& geom) noexcept {
    const svc::Toc& toc = geom.toc;
    const SaturnCddFrame::Comm cmd = comm_.comm();
    HeadLba cmd_head = cmd.head;
    std::uint32_t cmd_idx = cmd.index;
    svc::TrackNumber cmd_tno = cmd.track;

    constexpr std::uint32_t kIndexNum = 2;
    if (cmd_tno != svc::TrackNumber{}
        && cmd_idx > kIndexNum
        && toc.last.contains(cmd_tno.index())) {
        cmd_idx -= kIndexNum;
        const svc::TrackIndex next = cmd_tno.index() + svc::TrackCount{1};
        cmd_tno = next.number();
        cmd_head = start_of_(next);
    }
    const HeadLba head = cmd_head + offset_by_index_(cmd_tno, cmd_idx, geom);
    const std::int32_t speed = cmd.speed == 1 ? 1 : 2;

    switch (cmd.command) {
        case kCommSeekRing:
            seek_lba_ = head;
            track_ = track_by_lba_(seek_lba_, geom);
            index_ = index_by_lba_(track_, seek_lba_);
            pend_.seek_ring = true;
            pend_.read_pend = false;
            pend_.pause_pend = false;
            speed_ = speed;
            break;
        case kCommToc:
            toc_pos_ = 0;
            pend_.read_toc = true;
            speed_ = speed;
            break;
        case kCommStop:
            pend_.stop_pend = true;
            pend_.read_pend = false;
            break;
        case kCommRead: {
            const HeadLba lba_old = lba_;
            seek_lba_ = head - saturn::kReadHeadLead;
            lba_ = seek_lba_;
            track_ = track_by_lba_(seek_lba_, geom);
            index_ = index_by_lba_(track_, seek_lba_);
            pend_.read_pend = true;
            pend_.seek_pend = true;
            const HeadMove move{lba_old - saturn::kSeekOriginLead, head};
            const bool cross_track = track_by_lba_(move.from, geom) != track_by_lba_(head, geom);
            seek_delay_ = saturn::seek_delay_ticks(move, cross_track);
            pend_.pause_pend = false;
            speed_ = speed;
            audio_first_ = true;
            if (read_skips_seek_)
                pend_.seek_pend = false;
            prime_(geom);
            break;
        }
        case kCommPause:
            pend_.pause_pend = true;
            pend_.seek_pend = false;
            pend_.read_pend = false;
            break;
        case kCommSeek: {
            const HeadLba lba_old = lba_;
            seek_lba_ = head;
            lba_ = seek_lba_;
            track_ = track_by_lba_(seek_lba_, geom);
            index_ = index_by_lba_(track_, seek_lba_);
            pend_.seek_pend = true;
            const HeadMove move{lba_old - saturn::kSeekOriginLead, head};
            const bool cross_track = track_by_lba_(move.from, geom) != track_by_lba_(head, geom);
            seek_delay_ = saturn::seek_delay_ticks(move, cross_track);
            pend_.final_read = pend_.read_pend;
            pend_.read_pend = false;
            pend_.pause_pend = false;
            speed_ = speed;
            prime_(geom);
            break;
        }
        case kCommNop:
            break;
        default:
            pend_.seek_pend = false;
            pend_.read_pend = false;
            pend_.pause_pend = false;
            break;
    }

    if (!comm_.checksum_ok())
        ++stats_.crc_faults;
}

Ex<void> SaturnCddEngine::do_tick(CdDialectPorts& p) {

    if (header_owed_ && take_header_(p.geom)) {
        header_owed_ = false;
        if (auto r = p.link.send_data(
                io_.info,
                std::as_bytes(std::span<const std::uint8_t>(header_))
            );
            !r) {
            return r;
        }
    }
    process_(p.geom);
    std::array<std::uint16_t, kStatusWords> words{};
    for (std::size_t i = 0; i < words.size(); ++i)
        words[i] = stat_.word(i);
    auto status = send_status_words_(words, p.link);
    auto upd = update_(p);
    if (!status)
        return status;
    return upd;
}

void SaturnCddEngine::fill_position_(
    std::uint8_t code,
    std::uint8_t q,
    bool pause_msf,
    const svc::DiscGeometry& geom
) noexcept {
    const svc::Toc& toc = geom.toc;
    const Msf amsf = stock_msf_of(lba_ + kPregapSectors);
    const SectorCount rel = lba_ - start_of_(track_);
    Msf msf{};
    if (pause_msf) {
        msf = stock_msf_of(rel.magnitude() + kPregapSectors);
    } else if (lba_ < HeadLba{}) {
        msf = stock_msf_of(HeadLba{} - lba_);
    } else {
        msf = stock_msf_of(rel);
    }
    stat_.set(StatField::Status, code);
    stat_.set(StatField::Control, static_cast<std::uint8_t>(q | 0x01));
    stat_.set(
        StatField::Track,
        lba_ < head_of(toc.end) + kPregapSectors ? bcd(track_.number()) : kAbsentTrack
    );
    stat_.set(StatField::Index, lba_ < HeadLba{} ? 0x00 : bcd(index_));
    stat_.set(StatField::Minute, bcd(msf.m));
    stat_.set(StatField::Second, bcd(msf.s));
    stat_.set(StatField::Frame, bcd(msf.f));
    stat_.set(StatField::Zero, 0x00);
    stat_.set(StatField::AbsMinute, bcd(amsf.m));
    stat_.set(StatField::AbsSecond, bcd(amsf.s));
    stat_.set(StatField::AbsFrame, bcd(amsf.f));
}

void SaturnCddEngine::process_(const svc::DiscGeometry& geom) noexcept {

    const svc::Toc& toc = geom.toc;
    std::uint8_t q = lba_ < head_of(toc.end) && is_data_(track_, geom) ? 0x40 : 0x00;

    if (lid_open_) {
        state_ = State::Open;
        const std::uint8_t open[11] = {kStatOpen, 0x41, 0x01, 0x01, 0, 0, 0, 0, 0, 0, 0};
        std::memcpy(stat_.bytes.data(), open, sizeof open);
    } else if (pend_.read_toc) {
        state_ = State::ReadToc;
        Msf msf{0, 2, 0};
        std::uint8_t idx = 0;
        if (toc_pos_ < 0x100) {
            const svc::TrackIndex row = svc::TrackIndex::of(toc_pos_);
            HeadLba at = start_of_(row) + kPregapSectors;
            if (toc_at_index1_)
                at += offset_by_index_(row.number(), 1, geom);
            msf = stock_msf_of(at);
            idx = bcd(row.number());
            q = is_data_(row, geom) ? 0x40 : 0x00;
            ++toc_pos_;
            if (!toc.last.contains(svc::TrackIndex::of(toc_pos_)))
                toc_pos_ = 0x100;
        } else {
            if (toc_pos_ == 0x100) {
                msf = {1, 0, 0};
                idx = 0xA0;
                q = is_data_(svc::TrackIndex{}, geom) ? 0x40 : 0x00;
            } else if (toc_pos_ == 0x101) {
                msf = Msf::of_minute(toc.last);
                idx = 0xA1;
                q = is_data_(toc.last.last_index(), geom) ? 0x40 : 0x00;
            } else if (toc_pos_ == 0x102) {
                msf = stock_msf_of(head_of(toc.end) + kPregapSectors);
                idx = 0xA2;
                q = 0x00;
                pend_.read_toc = false;
            }
            ++toc_pos_;
        }
        stat_.set(StatField::Status, kStatToc);
        stat_.set(StatField::Control, static_cast<std::uint8_t>(q | 0x01));
        stat_.set(StatField::Track, 0x00);
        stat_.set(StatField::Index, idx);
        stat_.set(StatField::Minute, 0x00);
        stat_.set(StatField::Second, 0x00);
        stat_.set(StatField::Frame, 0x00);
        stat_.set(StatField::Zero, 0x00);
        stat_.set(StatField::AbsMinute, bcd(msf.m));
        stat_.set(StatField::AbsSecond, bcd(msf.s));
        stat_.set(StatField::AbsFrame, bcd(msf.f));
    } else if (pend_.seek_pend) {
        state_ = pend_.final_read ? State::Read : State::Seek;
        fill_position_(kStatSeek, q, false, geom);
        seek_delay_ -= saturn::SeekTicks{1};
        if (seek_delay_ <= saturn::SeekTicks{}) {
            seek_delay_ = saturn::SeekTicks{};
            pend_.seek_pend = false;
        }
        pend_.final_read = false;
    } else if (pend_.seek_ring || pend_.seek_ring2) {

        state_ = State::SeekRing;
        const auto fad = SaturnCddFrame::fad_bytes(lba_);
        const bool second = !pend_.seek_ring;
        stat_.set(
            StatField::Status,
            second ? static_cast<std::uint8_t>(kStatSeekRing | 0x04) : kStatSeekRing
        );
        stat_.set(StatField::Control, second ? 0x48 : 0x00);
        stat_.set(StatField::Track, second ? 0x5A : 0x00);
        stat_.set(StatField::Index, fad[0]);
        stat_.set(StatField::Minute, fad[1]);
        stat_.set(StatField::Second, fad[2]);
        stat_.set(StatField::Frame, second ? 0x0A : 0x00);
        stat_.set(StatField::Zero, second ? 0x0A : 0x00);
        stat_.set(StatField::AbsMinute, second ? 0x09 : 0x00);
        stat_.set(StatField::AbsSecond, second ? 0x09 : 0x00);
        stat_.set(StatField::AbsFrame, 0x00);
        if (seek_ring_time_ != 0) {
            --seek_ring_time_;
        } else {
            if (second) {
                pend_.seek_ring2 = false;
            } else {
                pend_.seek_ring = false;
                pend_.seek_ring2 = true;
            }
            seek_ring_time_ = 1;
        }
    } else if (pend_.read_pend) {
        state_ = State::Read;
        fill_position_(kStatData, q, false, geom);
    } else if (pend_.pause_pend) {
        state_ = State::Idle;
        fill_position_(kStatIdle, q, true, geom);
        pend_.pause_pend = false;
    } else if (pend_.stop_pend) {
        state_ = State::Stop;
        const std::uint8_t stop[11] = {kStatStop, 0x41, 0x01, 0x01, 0, 0, 0, 0, 0, 0, 0};
        std::memcpy(stat_.bytes.data(), stop, sizeof stop);
        pend_.stop_pend = false;
    } else {
        state_ = State::Idle;
        fill_position_(kStatIdle, q, true, geom);
    }
    stat_.seal();
}

Ex<void> SaturnCddEngine::update_(CdDialectPorts& p) {

    switch (state_) {
        case State::Idle:
            lba_ += SectorCount{1};
            if (lba_ > seek_lba_ + SectorCount{4})
                lba_ -= SectorCount{4};
            retrack_(p.geom);
            break;
        case State::Open:
        case State::ReadToc:
            break;
        case State::Read: {
            auto r = read_pass_(p);
            if (!r)
                return std::unexpected(r.error());
            return {};
        }
        case State::Stop:
            retrack_(p.geom);
            break;
        case State::Seek:
            if (!pend_.seek_pend)
                lba_ = seek_lba_;
            retrack_(p.geom);
            break;
        case State::SeekRing:
            lba_ = seek_lba_;
            retrack_(p.geom);
            break;
    }
    ++stats_.idle_ticks;
    return {};
}

Ex<SaturnCddEngine::Sent> SaturnCddEngine::read_pass_(CdDialectPorts& p) {
    const svc::Toc& toc = p.geom.toc;
    Ex<Sent> sent = Sent::Yes;
    if (lba_ >= head_of(toc.end)) {
        auto r = ring_sector_send_(p);
        if (!r)
            sent = std::unexpected(r.error());
    } else if (is_data_(track_, p.geom)) {

        sent = data_sector_send_(p);
    } else {
        sent = audio_sector_send_(p);
        if (sent && *sent == Sent::Yes)
            audio_first_ = false;
    }
    if (sent && *sent == Sent::Declined) {
        ++declines_;
        return sent;
    }
    lba_ += SectorCount{1};
    retrack_(p.geom);
    seek_lba_ = lba_;
    prime_(p.geom);
    return sent;
}

Ex<SaturnCddEngine::Sent> SaturnCddEngine::data_sector_send_(CdDialectPorts& p) {
    const HeadLba at = lba_ >= HeadLba{} ? lba_ : HeadLba{};
    const std::optional<svc::Lba> lba = at.readable();
    if (!lba)
        return Sent::Declined;

    const bool synth = sector_size_(track_, p.geom) == svc::kUserDataBytes
        || lba_ < start_of_(track_);
    const svc::SectorHeader header = synth ? header_for_(lba_) : svc::SectorHeader{};
    const std::span<std::uint8_t> dst(sec_);
    const svc::DiscTake got = facts_.disc.take(
        svc::DiscAsk{.form = svc::DiscForm::RawFrame, .lba = *lba, .header = header},
        std::as_writable_bytes(dst)
    );
    if (!got.is_bytes()) {
        if (!got.is_failed())
            return Sent::Declined;
        ++stats_.substitutes;

        if (header.named)
            std::memcpy(sec_, header.bytes.data(), header.bytes.size());
    }

    const bool boot = sec_[12] == 0x00 && sec_[13] == 0x02 && sec_[14] == 0x00 && sec_[15] == 0x01;
    if (!window_)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    if (auto w = window_->write(
            data_ring_.write().byte_offset(),
            std::as_bytes(std::span<const std::uint8_t>(sec_))
        );
        !w) {
        return std::unexpected(w.error());
    }
    data_ring_.advance_write();
    const SaturnSlotRing::Slot read_slot = data_ring_.take_read();
    const SaturnModeWord mode = (speed_ == 2 ? SaturnModeWord::speed_double() : SaturnModeWord{})
                                    .with(boot ? SaturnModeWord::boot() : SaturnModeWord{})
                                    .with_slot(read_slot);
    ++stats_.data_sectors;
    if (auto r = send_mode_(mode, p.link); !r)
        return std::unexpected(r.error());
    return Sent::Yes;
}

Ex<SaturnCddEngine::Sent> SaturnCddEngine::audio_sector_send_(CdDialectPorts& p) {

    const bool first = audio_first_;
    const std::size_t count = first ? 2 : 1;
    for (std::size_t k = 0; k < count; ++k) {
        const HeadLba want = lba_ + SectorCount{first ? static_cast<std::int32_t>(k) : 1};
        if (aud_[k].lba == want)
            continue;
        if (const std::optional<svc::Lba> at = want.readable()) {
            const std::span<std::uint8_t> dst(aud_[k].data);
            const svc::DiscTake got = facts_.disc.take(
                svc::DiscAsk{.form = svc::DiscForm::RawFrame, .lba = *at},
                std::as_writable_bytes(dst)
            );
            if (!got.is_bytes()) {
                if (!got.is_failed())
                    return Sent::Declined;
                ++stats_.substitutes;
            }
        } else {
            ++stats_.substitutes;
        }
        aud_[k].lba = want;
    }
    if (!window_)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 1});
    if (first)
        audio_ring_.rewind();
    SaturnSlotRing::Slot at = audio_ring_.write();
    for (std::size_t k = 0; k < count; ++k) {
        if (auto w = window_->write(
                at.byte_offset(),
                std::as_bytes(std::span<const std::uint8_t>(aud_[k].data))
            );
            !w) {
            return std::unexpected(w.error());
        }
        at = at.next();
    }
    aud_[0].lba.reset();
    aud_[1].lba.reset();
    if (first)
        audio_ring_.skip_write();
    audio_ring_.advance_write();
    const SaturnSlotRing::Slot read_slot = audio_ring_.take_read();
    const SaturnModeWord mode = SaturnModeWord::cdda().with_slot(read_slot);
    stats_.cdda_sectors += static_cast<std::uint32_t>(count);
    if (auto r = send_mode_(mode, p.link); !r)
        return std::unexpected(r.error());
    return Sent::Yes;
}

Ex<void> SaturnCddEngine::ring_sector_send_(CdDialectPorts& p) {

    const Msf msf = stock_msf_of(lba_ + kPregapSectors);
    const std::uint8_t header[kRingHeaderBytes] =
        {bcd(msf.m), bcd(msf.s), bcd(msf.f), 0x02, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x28, 0x00};
    saturn::fill_ring_body(std::span<std::uint8_t, svc::kRawFrameBytes>(sec_));
    std::memcpy(sec_ + saturn::kRingBodyFirst, header, kRingHeaderBytes);
    std::memset(sec_ + saturn::kRingBodyLast, 0, svc::kRawFrameBytes - saturn::kRingBodyLast);
    if (!window_)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 2});
    const std::span<const std::uint8_t> body(
        sec_ + saturn::kRingBodyFirst,
        svc::kRawFrameBytes - saturn::kRingBodyFirst
    );
    if (auto w = window_->write(saturn::kRingBodyFirst, std::as_bytes(body)); !w) {
        return std::unexpected(w.error());
    }
    ++stats_.data_sectors;
    const SaturnModeWord mode = (speed_ == 2 ? SaturnModeWord::speed_double() : SaturnModeWord{})
                                    .with(SaturnModeWord::ring());
    return send_mode_(mode, p.link);
}

Ex<void> SaturnCddEngine::send_mode_(SaturnModeWord mode, ICdLink& link) {

    const auto bytes = mode.wire_bytes();
    return link.send_data(io_.data, std::as_bytes(std::span<const std::uint8_t>(bytes)));
}

void SaturnCddEngine::prime_(const svc::DiscGeometry& geom) noexcept {

    const svc::Toc& toc = geom.toc;
    if (lba_ >= head_of(toc.end))
        return;
    const HeadLba at = lba_ >= HeadLba{} ? lba_ : HeadLba{};
    if (is_data_(track_, geom)) {

        if (const std::optional<svc::Lba> lba = at.readable()) {
            const bool synth = sector_size_(track_, geom) == svc::kUserDataBytes
                || lba_ < start_of_(track_);
            facts_.disc.hint(
                svc::DiscAsk{
                    .form = svc::DiscForm::RawFrame,
                    .lba = *lba,
                    .header = synth ? header_for_(lba_) : svc::SectorHeader{}
                }
            );
        }
        return;
    }
    const HeadLba first = audio_first_ ? lba_ : lba_ + SectorCount{1};
    for (HeadLba at2 = first; at2 <= lba_ + SectorCount{1}; at2 += SectorCount{1}) {
        if (const std::optional<svc::Lba> lba = at2.readable()) {
            facts_.disc.hint(svc::DiscAsk{.form = svc::DiscForm::RawFrame, .lba = *lba});
        }
    }
}

bool SaturnCddEngine::take_header_(const svc::DiscGeometry& geom) noexcept {

    const svc::Toc& toc = geom.toc;
    if (toc.last == svc::TrackCount{})
        return false;
    const bool cooked = toc.at(svc::TrackIndex{}).sector_size == svc::kUserDataBytes;
    const svc::DiscForm form = cooked ? svc::DiscForm::UserData : svc::DiscForm::RawFrame;
    const std::span<std::uint8_t> dst = cooked ? std::span<std::uint8_t>(sec_, svc::kUserDataBytes)
                                               : std::span<std::uint8_t>(sec_);
    const svc::DiscAsk ask{.form = form, .lba = svc::Lba{0}};
    if (const svc::DiscTake got = facts_.disc.take(ask, std::as_writable_bytes(dst));
        !got.is_bytes()) {
        if (!got.is_failed())
            return false;
        ++stats_.substitutes;
        std::memset(sec_, 0, sizeof sec_);
    }
    std::memcpy(header_, sec_ + (cooked ? 0 : kHeaderBytes), kBootHeaderBytes);
    const std::span<const std::uint8_t> h(header_);
    toc_at_index1_ = quirk_matches(row_.quirks, CdQuirk::TocStartsAtIndex1, h);
    read_skips_seek_ = quirk_matches(row_.quirks, CdQuirk::ReadSkipsSeek, h);
    return true;
}

Ex<std::span<const std::byte>> SaturnCddEngine::do_disc_descriptor(
    std::span<std::byte, kMaxDescriptorBytes> out,
    const svc::DiscGeometry& geom
) {

    if (!geom.mounted)
        return std::unexpected(Error{Errc::core_load, ERR_SITE(), 0});
    if (!take_header_(geom)) {
        header_owed_ = true;
        facts_.disc.hint(
            svc::DiscAsk{
                .form = geom.toc.at(svc::TrackIndex{}).sector_size == svc::kUserDataBytes
                    ? svc::DiscForm::UserData
                    : svc::DiscForm::RawFrame,
                .lba = svc::Lba{0}
            }
        );
        return std::span<const std::byte>{};
    }
    header_owed_ = false;
    std::memcpy(out.data(), header_, kBootHeaderBytes);
    const std::span<const std::byte> written = out.first<kBootHeaderBytes>();
    return written;
}

void SaturnCddEngine::do_notify_disc(bool loaded, const svc::DiscGeometry& geom) noexcept {

    toc_at_index1_ = false;
    read_skips_seek_ = false;
    header_owed_ = false;
    stock_toc_.learn(geom);
    if (!loaded) {
        do_reset_machine();
        return;
    }
    lid_open_ = false;
    pend_.stop_pend = true;
    if (geom.toc.last != svc::TrackCount{}) {
        facts_.disc.hint(
            svc::DiscAsk{
                .form = geom.toc.at(svc::TrackIndex{}).sector_size == svc::kUserDataBytes
                    ? svc::DiscForm::UserData
                    : svc::DiscForm::RawFrame,
                .lba = svc::Lba{0}
            }
        );
    }
}

void SaturnCddEngine::do_reset_machine() noexcept {

    state_ = State::Open;
    lid_open_ = true;
    pend_.reset();
    track_ = svc::TrackIndex{};
    lba_ = HeadLba{};
    seek_lba_ = HeadLba{};
    speed_ = 0;
    audio_first_ = false;
    aud_[0].lba.reset();
    aud_[1].lba.reset();
    reset_status_();
}

bool SaturnCddEngine::do_streaming(const svc::DiscGeometry&) const noexcept {

    return !lid_open_
        && !pend_.read_toc
        && !pend_.seek_pend
        && !pend_.seek_ring
        && !pend_.seek_ring2
        && pend_.read_pend;
}

CdEngine::DriveView SaturnCddEngine::do_drive_view(const svc::DiscGeometry& geom) const noexcept {
    const bool is_data = is_data_(track_, geom);
    return DriveView{
        .state = drive_state_(is_data),
        .track = track_,
        .lba = lba_,
        .audio_lba = lba_,
        .is_data = is_data
    };
}

CdDriveState SaturnCddEngine::drive_state_(bool is_data) const noexcept {
    switch (state_) {
        case State::Idle:
            return CdDriveState::Stopped;
        case State::Open:
            return CdDriveState::Open;
        case State::ReadToc:
            return CdDriveState::Toc;
        case State::Read:
            return is_data ? CdDriveState::Reading : CdDriveState::Playing;
        case State::Stop:
            return CdDriveState::Stopped;
        case State::Seek:
        case State::SeekRing:
            return CdDriveState::Seeking;
    }
    return CdDriveState::Stopped;
}

void SaturnCddEngine::retrack_(const svc::DiscGeometry& geom) noexcept {
    track_ = track_by_lba_(lba_, geom);
    index_ = index_by_lba_(track_, lba_);
}

svc::TrackIndex SaturnCddEngine::track_by_lba_(
    HeadLba lba,
    const svc::DiscGeometry& geom
) const noexcept {

    return track_scan(geom.toc, lba);
}

std::uint32_t SaturnCddEngine::index_by_lba_(svc::TrackIndex track, HeadLba lba) const noexcept {

    return lba < start_of_(track) ? 0 : 1;
}

SectorCount SaturnCddEngine::offset_by_index_(
    svc::TrackNumber tno,
    std::uint32_t idx,
    const svc::DiscGeometry& geom
) const noexcept {

    const svc::TrackCount last = geom.toc.last;
    svc::TrackIndex t = tno == svc::TrackNumber{} ? svc::TrackIndex{} : tno.index();
    if (!last.contains(t))
        t = last.next_index();
    if (idx != 1)
        return SectorCount{};
    return stock_toc_.index1_of(t);
}

HeadLba SaturnCddEngine::start_of_(svc::TrackIndex track) const noexcept {
    return stock_toc_.start_of(track);
}

bool SaturnCddEngine::is_data_(
    svc::TrackIndex track,
    const svc::DiscGeometry& geom
) const noexcept {
    const svc::Toc& toc = geom.toc;
    return toc.last.contains(track) && toc.at(track).type != svc::TrackType::Cdda;
}

std::uint32_t SaturnCddEngine::sector_size_(
    svc::TrackIndex track,
    const svc::DiscGeometry& geom
) const noexcept {
    const svc::Toc& toc = geom.toc;
    return toc.last.contains(track) ? toc.at(track).sector_size : 0u;
}

svc::SectorHeader SaturnCddEngine::header_for_(HeadLba head) noexcept {

    const Msf msf = stock_msf_of(head + kPregapSectors);
    const std::array<std::uint8_t, svc::kSectorHeaderBytes> bytes{
        0x00,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0x00,
        bcd(msf.m),
        bcd(msf.s),
        bcd(msf.f),
        0x01
    };
    svc::SectorHeader out{};
    for (std::size_t i = 0; i < bytes.size(); ++i)
        out.bytes[i] = static_cast<std::byte>(bytes[i]);
    out.named = true;
    return out;
}

}
