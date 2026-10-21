// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/saturn_cdd_engine.h"

#include <cstring>

#include "cores/saturn_seek.h"

namespace mister::cores {

namespace {

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

constexpr std::int32_t kFad = saturn::kFadToLbaBias;
constexpr std::size_t kUserData = 2048;
constexpr std::size_t kHeaderBytes = 16;
constexpr std::size_t kRingHeaderBytes = 12;

struct StockMsf {
    std::uint8_t m, s, f;
};

constexpr StockMsf stock_msf(std::int32_t lba) noexcept {
    return {
        static_cast<std::uint8_t>((lba / 75) / 60),
        static_cast<std::uint8_t>((lba / 75) % 60),
        static_cast<std::uint8_t>(lba % 75)
    };
}

static_assert(stock_msf(-4).f == 252 && stock_msf(-4).s == 0);

constexpr std::uint8_t stock_bcd(std::int32_t v) noexcept {
    return static_cast<std::uint8_t>(((v / 10) << 4) | (v % 10));
}

static_assert(stock_bcd(252) == 0x92 && stock_bcd(99) == 0x99);

}

void SaturnCddEngine::reset_status_() noexcept {

    stat_[0] = kStatOpen;
    for (std::size_t i = 1; i <= 9; ++i)
        stat_[i] = 0;
    stat_[11] = 0;
    stat_[11] = saturn::frame_checksum(
        std::span<const std::uint8_t, saturn::kFrameSumBytes>(stat_, saturn::kFrameSumBytes)
    );
}

Ex<void> SaturnCddEngine::do_accept(std::span<const std::uint16_t> words) {

    for (std::size_t i = 0; i < 6; ++i) {
        comm_[2 * i] = static_cast<std::uint8_t>(words[i] & 0xFFu);
        comm_[2 * i + 1] = static_cast<std::uint8_t>(words[i] >> 8);
    }
    command_exec_();
    return {};
}

void SaturnCddEngine::command_exec_() noexcept {
    const svc::Toc& toc = ports_.geom.toc;
    const std::int32_t last = toc.last;
    std::int32_t cmd_fad = (static_cast<std::int32_t>(comm_[1]) << 16)
        | (static_cast<std::int32_t>(comm_[2]) << 8)
        | static_cast<std::int32_t>(comm_[3]);
    std::int32_t cmd_idx = comm_[4];
    std::int32_t cmd_tno = comm_[8];

    constexpr std::int32_t kIndexNum = 2;
    if (cmd_tno >= 1 && cmd_idx > kIndexNum && cmd_tno - 1 < last) {
        cmd_idx -= kIndexNum;
        cmd_tno += 1;
        cmd_fad = start_of_(cmd_tno - 1) + kFad;
    }
    const std::int32_t fad = cmd_fad + offset_by_index_(cmd_tno, cmd_idx);
    const std::int32_t speed = comm_[10] == 1 ? 1 : 2;

    switch (comm_[0]) {
        case kCommSeekRing:
            seek_lba_ = fad - kFad;
            track_ = track_by_lba_(seek_lba_);
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
            const std::int32_t lba_old = lba_;
            seek_lba_ = fad - kFad - saturn::kReadHeadLead;
            lba_ = seek_lba_;
            track_ = track_by_lba_(seek_lba_);
            index_ = index_by_lba_(track_, seek_lba_);
            pend_.read_pend = true;
            pend_.seek_pend = true;
            const std::int32_t from = lba_old - saturn::kSeekOriginLead;
            seek_delay_ = static_cast<std::int32_t>(saturn::seek_delay_ticks(
                from,
                fad - kFad,
                static_cast<std::uint32_t>(track_by_lba_(from)),
                static_cast<std::uint32_t>(track_by_lba_(fad - kFad))
            ));
            pend_.pause_pend = false;
            speed_ = speed;
            audio_first_ = true;
            if (read_skips_seek_)
                pend_.seek_pend = false;
            prime_();
            break;
        }
        case kCommPause:
            pend_.pause_pend = true;
            pend_.seek_pend = false;
            pend_.read_pend = false;
            break;
        case kCommSeek: {
            const std::int32_t lba_old = lba_;
            seek_lba_ = fad - kFad;
            lba_ = seek_lba_;
            track_ = track_by_lba_(seek_lba_);
            index_ = index_by_lba_(track_, seek_lba_);
            pend_.seek_pend = true;
            const std::int32_t from = lba_old - saturn::kSeekOriginLead;
            seek_delay_ = static_cast<std::int32_t>(saturn::seek_delay_ticks(
                from,
                fad - kFad,
                static_cast<std::uint32_t>(track_by_lba_(from)),
                static_cast<std::uint32_t>(track_by_lba_(fad - kFad))
            ));
            pend_.final_read = pend_.read_pend;
            pend_.read_pend = false;
            pend_.pause_pend = false;
            speed_ = speed;
            prime_();
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

    const std::uint8_t sum = saturn::frame_checksum(
        std::span<const std::uint8_t, saturn::kFrameSumBytes>(comm_, saturn::kFrameSumBytes)
    );
    if (sum != comm_[11])
        ++stats_.crc_faults;
}

Ex<void> SaturnCddEngine::do_tick() {

    if (header_owed_ && take_header_()) {
        header_owed_ = false;
        if (auto r = ports_.tx.send_data(
                ports_.profile.io.info,
                std::as_bytes(std::span<const std::uint8_t>(header_))
            );
            !r) {
            return r;
        }
    }
    process_();
    auto status = ports_.tx.send_status(0);
    auto upd = update_();
    if (!status)
        return status;
    return upd;
}

void SaturnCddEngine::fill_position_(std::uint8_t code, std::uint8_t q, bool pause_msf) noexcept {
    const svc::Toc& toc = ports_.geom.toc;
    const StockMsf amsf = stock_msf(lba_ + kFad);
    const std::int32_t rel = lba_ - start_of_(track_);
    StockMsf msf{};
    if (pause_msf) {
        msf = stock_msf((rel < 0 ? -rel : rel) + kFad);
    } else {
        msf = lba_ < 0 ? stock_msf(-lba_) : stock_msf(rel);
    }
    stat_[0] = code;
    stat_[1] = static_cast<std::uint8_t>(q | 0x01);
    stat_[2] = lba_ < static_cast<std::int32_t>(toc.end.v) + kFad ? stock_bcd(track_ + 1) : 0xAA;
    stat_[3] = lba_ < 0 ? 0x00 : stock_bcd(index_);
    stat_[4] = stock_bcd(msf.m);
    stat_[5] = stock_bcd(msf.s);
    stat_[6] = stock_bcd(msf.f);
    stat_[7] = 0x00;
    stat_[8] = stock_bcd(amsf.m);
    stat_[9] = stock_bcd(amsf.s);
    stat_[10] = stock_bcd(amsf.f);
}

void SaturnCddEngine::process_() noexcept {

    const svc::Toc& toc = ports_.geom.toc;
    const std::int32_t last = toc.last;
    std::uint8_t q = lba_ < static_cast<std::int32_t>(toc.end.v) && is_data_(track_) ? 0x40 : 0x00;

    if (lid_open_) {
        state_ = State::Open;
        const std::uint8_t open[11] = {kStatOpen, 0x41, 0x01, 0x01, 0, 0, 0, 0, 0, 0, 0};
        std::memcpy(stat_, open, sizeof open);
    } else if (pend_.read_toc) {
        state_ = State::ReadToc;
        StockMsf msf{0, 2, 0};
        std::uint8_t idx = 0;
        if (toc_pos_ < 0x100) {
            std::int32_t at = start_of_(toc_pos_) + kFad;
            if (toc_at_index1_)
                at += offset_by_index_(toc_pos_ + 1, 1);
            msf = stock_msf(at);
            idx = stock_bcd(toc_pos_ + 1);
            q = is_data_(toc_pos_ & 0xFF) ? 0x40 : 0x00;
            ++toc_pos_;
            if (toc_pos_ >= last)
                toc_pos_ = 0x100;
        } else {
            if (toc_pos_ == 0x100) {
                msf = {1, 0, 0};
                idx = 0xA0;
                q = is_data_(0) ? 0x40 : 0x00;
            } else if (toc_pos_ == 0x101) {
                msf = {static_cast<std::uint8_t>(last), 0, 0};
                idx = 0xA1;
                q = is_data_(last - 1) ? 0x40 : 0x00;
            } else if (toc_pos_ == 0x102) {
                msf = stock_msf(static_cast<std::int32_t>(toc.end.v) + kFad);
                idx = 0xA2;
                q = 0x00;
                pend_.read_toc = false;
            }
            ++toc_pos_;
        }
        stat_[0] = kStatToc;
        stat_[1] = static_cast<std::uint8_t>(q | 0x01);
        stat_[2] = 0x00;
        stat_[3] = idx;
        stat_[4] = 0x00;
        stat_[5] = 0x00;
        stat_[6] = 0x00;
        stat_[7] = 0x00;
        stat_[8] = stock_bcd(msf.m);
        stat_[9] = stock_bcd(msf.s);
        stat_[10] = stock_bcd(msf.f);
    } else if (pend_.seek_pend) {
        state_ = pend_.final_read ? State::Read : State::Seek;
        fill_position_(kStatSeek, q, false);
        --seek_delay_;
        if (seek_delay_ <= 0) {
            seek_delay_ = 0;
            pend_.seek_pend = false;
        }
        pend_.final_read = false;
    } else if (pend_.seek_ring || pend_.seek_ring2) {

        state_ = State::SeekRing;
        const std::int32_t fad = lba_ + kFad;
        const bool second = !pend_.seek_ring;
        stat_[0] = second ? static_cast<std::uint8_t>(kStatSeekRing | 0x04) : kStatSeekRing;
        stat_[1] = second ? 0x48 : 0x00;
        stat_[2] = second ? 0x5A : 0x00;
        stat_[3] = static_cast<std::uint8_t>(fad >> 16);
        stat_[4] = static_cast<std::uint8_t>(fad >> 8);
        stat_[5] = static_cast<std::uint8_t>(fad);
        stat_[6] = second ? 0x0A : 0x00;
        stat_[7] = second ? 0x0A : 0x00;
        stat_[8] = second ? 0x09 : 0x00;
        stat_[9] = second ? 0x09 : 0x00;
        stat_[10] = 0x00;
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
        fill_position_(kStatData, q, false);
    } else if (pend_.pause_pend) {
        state_ = State::Idle;
        fill_position_(kStatIdle, q, true);
        pend_.pause_pend = false;
    } else if (pend_.stop_pend) {
        state_ = State::Stop;
        const std::uint8_t stop[11] = {kStatStop, 0x41, 0x01, 0x01, 0, 0, 0, 0, 0, 0, 0};
        std::memcpy(stat_, stop, sizeof stop);
        pend_.stop_pend = false;
    } else {
        state_ = State::Idle;
        fill_position_(kStatIdle, q, true);
    }
    stat_[11] = saturn::frame_checksum(
        std::span<const std::uint8_t, saturn::kFrameSumBytes>(stat_, saturn::kFrameSumBytes)
    );
}

Ex<void> SaturnCddEngine::update_() {

    switch (state_) {
        case State::Idle:
            ++lba_;
            if (lba_ > seek_lba_ + 4)
                lba_ -= 4;
            retrack_();
            break;
        case State::Open:
        case State::ReadToc:
            break;
        case State::Read: {
            auto r = read_pass_();
            if (!r)
                return std::unexpected(r.error());
            return {};
        }
        case State::Pause:
        case State::Stop:
            retrack_();
            break;
        case State::Seek:
        case State::SeekRead:
            if (!pend_.seek_pend)
                lba_ = seek_lba_;
            retrack_();
            break;
        case State::SeekRing:
            lba_ = seek_lba_;
            retrack_();
            break;
    }
    ++stats_.idle_ticks;
    return {};
}

Ex<SaturnCddEngine::Sent> SaturnCddEngine::read_pass_() {
    const svc::Toc& toc = ports_.geom.toc;
    Ex<Sent> sent = Sent::Yes;
    if (lba_ >= static_cast<std::int32_t>(toc.end.v)) {
        auto r = ring_sector_send_();
        if (!r)
            sent = std::unexpected(r.error());
    } else if (is_data_(track_)) {

        const bool synth = sector_size_(track_) == kUserData || lba_ - start_of_(track_) < 0;
        sent = data_sector_send_(synth);
    } else {
        sent = audio_sector_send_();
        if (sent && *sent == Sent::Yes)
            audio_first_ = false;
    }
    if (sent && *sent == Sent::Declined) {
        ++declines_;
        return sent;
    }
    ++lba_;
    retrack_();
    seek_lba_ = lba_;
    prime_();
    return sent;
}

Ex<SaturnCddEngine::Sent> SaturnCddEngine::data_sector_send_(bool synth_header) {
    const std::int32_t at = lba_ >= 0 ? lba_ : 0;
    const bool cooked = sector_size_(track_) == kUserData;
    const svc::DiscForm form = cooked ? svc::DiscForm::UserData : svc::DiscForm::RawFrame;
    const std::span<std::uint8_t> dst = cooked
        ? std::span<std::uint8_t>(sec_ + kHeaderBytes, kUserData)
        : std::span<std::uint8_t>(sec_);
    const svc::Lba lba{static_cast<std::uint32_t>(at)};
    if (ports_.disc.take(form, lba, std::as_writable_bytes(dst)) == 0) {
        if (!ports_.disc.take_read_failed(form, lba))
            return Sent::Declined;
        ++stats_.substitutes;
    }
    if (synth_header) {
        const StockMsf msf = stock_msf(lba_ + kFad);
        const std::uint8_t header[kHeaderBytes] = {
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
            stock_bcd(msf.m),
            stock_bcd(msf.s),
            stock_bcd(msf.f),
            0x01
        };
        std::memcpy(sec_, header, kHeaderBytes);
    }

    const bool mode2 = sec_[15] == 0x02;
    const std::uint32_t crc = saturn::edc(
        std::span<const std::uint8_t>(sec_, mode2 ? saturn::kMode2EdcSpan : saturn::kMode1EdcSpan)
    );
    if (!mode2) {
        sec_[saturn::kMode1EdcOffset + 0] = static_cast<std::uint8_t>(crc);
        sec_[saturn::kMode1EdcOffset + 1] = static_cast<std::uint8_t>(crc >> 8);
        sec_[saturn::kMode1EdcOffset + 2] = static_cast<std::uint8_t>(crc >> 16);
        sec_[saturn::kMode1EdcOffset + 3] = static_cast<std::uint8_t>(crc >> 24);
        std::memset(
            sec_ + saturn::kMode1TailFirst,
            0,
            saturn::kSectorBytes - saturn::kMode1TailFirst
        );
    }
    const bool boot = sec_[12] == 0x00 && sec_[13] == 0x02 && sec_[14] == 0x00 && sec_[15] == 0x01;
    if (window_ == nullptr)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    if (auto w = window_->write(
            data_ring_.write() * kSlotBytes,
            std::as_bytes(std::span<const std::uint8_t>(sec_))
        );
        !w) {
        return std::unexpected(w.error());
    }
    data_ring_.advance_write();
    const std::uint8_t read_slot = data_ring_.take_read();
    const auto mode = static_cast<std::uint16_t>(
        (speed_ == 2 ? 0x0101 : 0x0000)
        | (boot ? 0x0808 : 0x0000)
        | (read_slot << 4)
        | (read_slot << 12)
    );
    ++stats_.data_sectors;
    if (auto r = send_mode_(mode); !r)
        return std::unexpected(r.error());
    return Sent::Yes;
}

Ex<SaturnCddEngine::Sent> SaturnCddEngine::audio_sector_send_() {

    const bool first = audio_first_;
    const std::size_t count = first ? 2 : 1;
    for (std::size_t k = 0; k < count; ++k) {
        const std::int64_t want = static_cast<std::int64_t>(lba_)
            + (first ? static_cast<std::int64_t>(k) : 1);
        if (aud_lba_[k] == want)
            continue;
        if (want >= 0) {
            const svc::Lba at{static_cast<std::uint32_t>(want)};
            const std::span<std::uint8_t> dst(aud_[k]);
            if (ports_.disc.take(svc::DiscForm::RawFrame, at, std::as_writable_bytes(dst)) == 0) {
                if (!ports_.disc.take_read_failed(svc::DiscForm::RawFrame, at))
                    return Sent::Declined;
                ++stats_.substitutes;
            }
        } else {
            ++stats_.substitutes;
        }
        aud_lba_[k] = want;
    }
    if (window_ == nullptr)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 1});
    if (first)
        audio_ring_.rewind();
    for (std::size_t k = 0; k < count; ++k) {
        if (auto w = window_->write(
                (audio_ring_.write() + k) * kSlotBytes,
                std::as_bytes(std::span<const std::uint8_t>(aud_[k]))
            );
            !w) {
            return std::unexpected(w.error());
        }
    }
    aud_lba_[0] = aud_lba_[1] = -1;
    if (first)
        audio_ring_.skip_write();
    audio_ring_.advance_write();
    const std::uint8_t read_slot = audio_ring_.take_read();
    const auto mode = static_cast<std::uint16_t>(0x0202 | (read_slot << 4) | (read_slot << 12));
    stats_.cdda_sectors += static_cast<std::uint32_t>(count);
    if (auto r = send_mode_(mode); !r)
        return std::unexpected(r.error());
    return Sent::Yes;
}

Ex<void> SaturnCddEngine::ring_sector_send_() {

    const StockMsf msf = stock_msf(lba_ + kFad);
    const std::uint8_t header[kRingHeaderBytes] = {
        stock_bcd(msf.m),
        stock_bcd(msf.s),
        stock_bcd(msf.f),
        0x02,
        0x00,
        0x00,
        0x28,
        0x00,
        0x00,
        0x00,
        0x28,
        0x00
    };
    saturn::fill_ring_body(std::span<std::uint8_t, saturn::kSectorBytes>(sec_));
    std::memcpy(sec_ + saturn::kRingBodyFirst, header, kRingHeaderBytes);
    std::memset(sec_ + saturn::kRingBodyLast, 0, saturn::kSectorBytes - saturn::kRingBodyLast);
    if (window_ == nullptr)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 2});
    const std::span<const std::uint8_t> body(
        sec_ + saturn::kRingBodyFirst,
        saturn::kSectorBytes - saturn::kRingBodyFirst
    );
    if (auto w = window_->write(saturn::kRingBodyFirst, std::as_bytes(body)); !w) {
        return std::unexpected(w.error());
    }
    ++stats_.data_sectors;
    return send_mode_(static_cast<std::uint16_t>((speed_ == 2 ? 0x0101 : 0x0000) | 0x0404));
}

Ex<void> SaturnCddEngine::send_mode_(std::uint16_t mode) {

    const std::uint8_t bytes[2] = {
        static_cast<std::uint8_t>(mode & 0xFFu),
        static_cast<std::uint8_t>(mode >> 8)
    };
    return ports_.tx.send_data(
        ports_.profile.io.data,
        std::as_bytes(std::span<const std::uint8_t>(bytes))
    );
}

void SaturnCddEngine::prime_() noexcept {

    const svc::Toc& toc = ports_.geom.toc;
    if (lba_ >= static_cast<std::int32_t>(toc.end.v))
        return;
    if (is_data_(track_)) {
        const std::int32_t at = lba_ >= 0 ? lba_ : 0;
        ports_.disc.hint(
            sector_size_(track_) == kUserData ? svc::DiscForm::UserData : svc::DiscForm::RawFrame,
            svc::Lba{static_cast<std::uint32_t>(at)}
        );
        return;
    }
    const std::int32_t first = audio_first_ ? lba_ : lba_ + 1;
    for (std::int32_t at = first; at <= lba_ + 1; ++at) {
        if (at >= 0)
            ports_.disc.hint(svc::DiscForm::RawFrame, svc::Lba{static_cast<std::uint32_t>(at)});
    }
}

bool SaturnCddEngine::take_header_() noexcept {

    const svc::Toc& toc = ports_.geom.toc;
    if (toc.last == 0)
        return false;
    const bool cooked = toc.tracks[0].sector_size == kUserData;
    const svc::DiscForm form = cooked ? svc::DiscForm::UserData : svc::DiscForm::RawFrame;
    const std::span<std::uint8_t> dst = cooked ? std::span<std::uint8_t>(sec_, kUserData)
                                               : std::span<std::uint8_t>(sec_);
    if (ports_.disc.take(form, svc::Lba{0}, std::as_writable_bytes(dst)) == 0) {
        if (!ports_.disc.take_read_failed(form, svc::Lba{0}))
            return false;
        ++stats_.substitutes;
        std::memset(sec_, 0, sizeof sec_);
    }
    std::memcpy(header_, sec_ + (cooked ? 0 : kHeaderBytes), kBootHeaderBytes);
    const std::span<const std::uint8_t> h(header_);
    toc_at_index1_ = quirk_matches(ports_.profile.quirks, CdQuirk::TocStartsAtIndex1, h);
    read_skips_seek_ = quirk_matches(ports_.profile.quirks, CdQuirk::ReadSkipsSeek, h);
    return true;
}

Ex<std::size_t> SaturnCddEngine::do_disc_descriptor(std::span<std::byte> out) {

    if (!ports_.geom.mounted)
        return std::unexpected(Error{Errc::core_load, ERR_SITE(), 0});
    if (out.size() < kBootHeaderBytes)
        return std::unexpected(Error{Errc::core_load, ERR_SITE(), 1});
    if (!take_header_()) {
        header_owed_ = true;
        ports_.disc.hint(
            ports_.geom.toc.tracks[0].sector_size == kUserData ? svc::DiscForm::UserData
                                                               : svc::DiscForm::RawFrame,
            svc::Lba{0}
        );
        return std::size_t{0};
    }
    header_owed_ = false;
    std::memcpy(out.data(), header_, kBootHeaderBytes);
    return kBootHeaderBytes;
}

Ex<void> SaturnCddEngine::do_notify_disc(bool loaded) {

    toc_at_index1_ = false;
    read_skips_seek_ = false;
    header_owed_ = false;
    stock_toc_.learn(ports_.geom);
    if (!loaded) {
        do_reset_machine();
        return {};
    }
    lid_open_ = false;
    pend_.stop_pend = true;
    if (ports_.geom.toc.last != 0) {
        ports_.disc.hint(
            ports_.geom.toc.tracks[0].sector_size == kUserData ? svc::DiscForm::UserData
                                                               : svc::DiscForm::RawFrame,
            svc::Lba{0}
        );
    }
    return {};
}

void SaturnCddEngine::do_reset_machine() noexcept {

    state_ = State::Open;
    lid_open_ = true;
    pend_.reset();
    track_ = 0;
    lba_ = 0;
    seek_lba_ = 0;
    speed_ = 0;
    audio_first_ = false;
    aud_lba_[0] = aud_lba_[1] = -1;
    reset_status_();
}

bool SaturnCddEngine::do_streaming() const noexcept {

    return !lid_open_
        && !pend_.read_toc
        && !pend_.seek_pend
        && !pend_.seek_ring
        && !pend_.seek_ring2
        && pend_.read_pend;
}

CdEngine::DriveView SaturnCddEngine::do_drive_view() const noexcept {
    return DriveView{
        static_cast<std::uint8_t>(state_),
        static_cast<std::uint32_t>(track_),
        lba_,
        lba_,
        is_data_(track_)
    };
}

std::size_t SaturnCddEngine::do_status_words(
    std::uint64_t,
    std::span<std::uint16_t, kMaxStatusWords> out
) const noexcept {

    for (std::size_t i = 0; i < 6; ++i) {
        out[i] = static_cast<std::uint16_t>(stat_[2 * i] | (stat_[2 * i + 1] << 8));
    }
    return 6;
}

void SaturnCddEngine::retrack_() noexcept {
    track_ = track_by_lba_(lba_);
    index_ = index_by_lba_(track_, lba_);
}

std::int32_t SaturnCddEngine::track_by_lba_(std::int32_t lba) const noexcept {

    const svc::Toc& toc = ports_.geom.toc;
    std::int32_t i = 0;
    while (i < toc.last && static_cast<std::int64_t>(toc.tracks[i].end.v) <= lba)
        ++i;
    return i;
}

std::int32_t SaturnCddEngine::index_by_lba_(std::int32_t track, std::int32_t lba) const noexcept {

    return lba - start_of_(track) < 0 ? 0 : 1;
}

std::int32_t SaturnCddEngine::offset_by_index_(std::int32_t tno, std::int32_t idx) const noexcept {

    const std::int32_t last = ports_.geom.toc.last;
    std::int32_t t = tno - 1;
    if (t < 0)
        t = 0;
    else if (t > last)
        t = last;
    if (idx > 99)
        idx = 99;
    if (idx <= 0)
        return 0;
    if (idx != 1)
        return 0;
    return stock_toc_.index1_of(t);
}

std::int32_t SaturnCddEngine::start_of_(std::int32_t track) const noexcept {
    return stock_toc_.start_of(track);
}

bool SaturnCddEngine::is_data_(std::int32_t track) const noexcept {
    const svc::Toc& toc = ports_.geom.toc;
    return track >= 0 && track < toc.last && toc.tracks[track].type != svc::TrackType::Cdda;
}

std::uint32_t SaturnCddEngine::sector_size_(std::int32_t track) const noexcept {
    const svc::Toc& toc = ports_.geom.toc;
    return track >= 0 && track < toc.last ? toc.tracks[track].sector_size : 0u;
}

}
