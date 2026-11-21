// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cd/threedo_cdd_engine.h"

#include <array>
#include <cstring>

#include "cd_core_detail.h"
#include "cores/cd/cd_command_words.h"
#include "cores/cd/cd_head_prime.h"
#include "cores/cd_msf.h"
#include "infra/byte_view.h"
#include "infra/prelude.h"

namespace mister::cores::cd {

namespace {

constexpr std::array<hal::SpiWord, 4> kStatusWords{};

constexpr std::uint8_t kDiscDaOrCdrom = 0x00;

constexpr std::size_t kHeaderBytes = 16;
constexpr std::size_t kChecksumBytes = 2;
constexpr std::size_t kInfoTailBytes = 128;
constexpr std::size_t kInfoTailOffset = 1024;

}

ThreeDoCddEngine::CddCommand ThreeDoCddEngine::CddCommand::of(
    std::span<const std::uint8_t, kBytes> bytes
) noexcept {
    CddCommand out;
    switch (bytes[0]) {
        case static_cast<std::uint8_t>(Op::Nop):
            out.op = Op::Nop;
            break;
        case static_cast<std::uint8_t>(Op::Read):
            out.op = Op::Read;
            break;
        case static_cast<std::uint8_t>(Op::Next):
            out.op = Op::Next;
            break;
        default:
            break;
    }

    const std::uint32_t raw = (std::uint32_t{bytes[1]} << 16)
        | (std::uint32_t{bytes[2]} << 8)
        | std::uint32_t{bytes[3]};
    out.target = bytes[4] != 0 ? HeadLba::of(proto::Lba{raw})
                               : HeadLba::of(svc::Msf{bytes[1], bytes[2], bytes[3]});
    out.blocks = SectorCount::of(
        proto::BlockCount{(std::uint32_t{bytes[5]} << 8) | std::uint32_t{bytes[6]}}
    );
    return out;
}

Ex<void> ThreeDoCddEngine::do_accept(const CdCommandWords& cmd, CdDialectPorts& p) {
    std::array<std::uint8_t, CddCommand::kBytes> b{};
    cmd.store_le(b);
    command_exec_(CddCommand::of(b), p.geom);
    return {};
}

void ThreeDoCddEngine::command_exec_(
    const CddCommand& command,
    const svc::disc::DiscGeometry& geom
) {
    switch (command.op) {
        case Op::Nop:
            break;
        case Op::Read:
            lba_ = command.target - kPregapSectors;
            hint_at_(lba_, geom);
            track_ = track_scan(geom.toc, lba_);
            read_pend_ = true;
            block_reads_ = command.blocks;
            break;
        case Op::Next:

            if (block_reads_ > SectorCount{})
                read_pend_ = true;
            break;
        case Op::Refused:
            read_pend_ = false;
            break;
    }
}

Ex<void> ThreeDoCddEngine::do_tick(CdDialectPorts& p) {

    if (read_pend_ && p.egress_busy()) {
        ++stats_.busy_ticks;
        return send_status_words_(kStatusWords, p.link);
    }
    if (read_pend_) {
        state_ = State::Read;
        read_pend_ = false;
    } else {
        state_ = State::Idle;
    }
    if (auto r = send_status_words_(kStatusWords, p.link); !r)
        return r;
    return update_(p);
}

Ex<void> ThreeDoCddEngine::update_(CdDialectPorts& p) {
    const svc::disc::Toc& toc = p.geom.toc;
    if (state_ != State::Read) {

        if (state_ != State::Open)
            track_ = track_scan(p.geom.toc, lba_);
        ++stats_.idle_ticks;
        return {};
    }

    Ex<void> sent{};
    if (toc.last.contains(track_) && toc.tracks[track_].type == svc::disc::TrackType::Mode1) {
        const svc::disc::Track& tk = toc.tracks[track_];

        if (toc.sector_size == svc::disc::kUserDataBytes || lba_ < head_of(tk.start)) {
            const Msf msf = msf_of(lba_ + kPregapSectors);
            std::uint8_t header[kHeaderBytes];
            header[0] = 0x00;
            for (std::size_t i = 1; i <= 10; ++i)
                header[i] = 0xFF;
            header[11] = 0x00;
            header[12] = bcd(msf.m);
            header[13] = bcd(msf.s);
            header[14] = bcd(msf.f);
            header[15] = static_cast<std::uint8_t>(tk.type != svc::disc::TrackType::Cdda);
            sent = data_sector_send_(std::span<const std::uint8_t>(header), p);
        } else {
            sent = data_sector_send_({}, p);
        }
    }

    lba_ += SectorCount{1};
    track_ = track_scan(p.geom.toc, lba_);
    block_reads_ -= SectorCount{1};
    return sent;
}

Ex<void> ThreeDoCddEngine::data_sector_send_(
    std::span<const std::uint8_t> header,
    CdDialectPorts& p
) {

    read_data_(p.geom);
    if (!header.empty())
        std::memcpy(sec_buf_, header.data(), header.size());

    std::uint16_t crc = 0;
    for (std::size_t i = 0; i < svc::disc::kUserDataBytes; ++i) {
        crc = static_cast<std::uint16_t>(crc + sec_buf_[kHeaderBytes + i]);
    }
    sec_buf_[kHeaderBytes + svc::disc::kUserDataBytes] = static_cast<std::uint8_t>(crc);
    sec_buf_[kHeaderBytes + svc::disc::kUserDataBytes + 1] = static_cast<std::uint8_t>(crc >> 8);

    ++stats_.data_sectors;
    return p.link.send_data(
        facts_.profile.io.data,
        std::as_bytes(
            std::span<const std::uint8_t>(
                sec_buf_ + kHeaderBytes,
                svc::disc::kUserDataBytes + kChecksumBytes
            )
        )
    );
}

void ThreeDoCddEngine::hint_at_(HeadLba lba, const svc::disc::DiscGeometry& geom) noexcept {
    const std::optional<svc::disc::Lba> at = lba.readable();
    if (!at)
        return;
    const svc::disc::Toc& toc = geom.toc;

    prime_head(
        facts_.disc,
        *at,
        {.form = toc.sector_size == svc::disc::kUserDataBytes ? svc::disc::DiscForm::UserData
                                                              : svc::disc::DiscForm::RawFrame}
    );
}

void ThreeDoCddEngine::read_data_(const svc::disc::DiscGeometry& geom) {

    const svc::disc::Toc& toc = geom.toc;
    if (!toc.last.contains(track_) || toc.tracks[track_].type != svc::disc::TrackType::Mode1)
        return;
    const HeadLba head = lba_ > HeadLba{} ? lba_ : HeadLba{};
    const std::optional<svc::disc::Lba> at = head.readable();
    if (!at)
        return;

    const svc::disc::DiscForm form = toc.sector_size == svc::disc::kUserDataBytes
        ? svc::disc::DiscForm::UserData
        : svc::disc::DiscForm::RawFrame;
    const std::span<std::uint8_t> dst = form == svc::disc::DiscForm::UserData
        ? std::span<std::uint8_t>(sec_buf_ + kHeaderBytes, svc::disc::kUserDataBytes)
        : std::span<std::uint8_t>(sec_buf_, svc::disc::kRawFrameBytes);
    const bool got = facts_.disc
                         .take(
                             svc::disc::DiscAsk{.form = form, .lba = *at},
                             std::as_writable_bytes(dst)
                         )
                         .is_bytes();
    if (!got)
        ++stats_.substitutes;
    if (const std::optional<svc::disc::Lba> next = (head + SectorCount{1}).readable()) {
        facts_.disc.hint(svc::disc::DiscAsk{.form = form, .lba = *next});
    }
}

Ex<void> ThreeDoCddEngine::build_disc_info(
    std::span<std::byte, kDiscInfoBytes> out,
    const svc::disc::DiscGeometry& geom
) {

    if (!geom.mounted)
        return std::unexpected(Error{Errc::core_load, ERR_SITE(), 0});

    const svc::disc::Toc& toc = geom.toc;
    std::memset(out.data(), 0x00, out.size());
    const std::span<std::uint8_t> bytes = infra::byte_cast<std::uint8_t>(out);
    std::uint8_t* const b = bytes.data();

    const Msf end = msf_of(head_of(toc.end) + kPregapSectors);
    constexpr std::size_t kTrackByte = 3;
    b[1] = kDiscDaOrCdrom;
    b[2] = 0x01;
    toc.last.store_byte(bytes.subspan(kTrackByte).first<1>());
    b[4] = static_cast<std::uint8_t>(end.m);
    b[5] = static_cast<std::uint8_t>(end.s);
    b[6] = static_cast<std::uint8_t>(end.f);

    std::uint32_t offs = 0x10;
    for (svc::TrackIndex row{}; row <= toc.last.next_index();
         row = row + svc::TrackCount{1}, offs += 0x10) {

        const bool lead_out = !toc.last.contains(row);
        const HeadLba start = lead_out ? head_of(toc.end) : head_of(toc.tracks[row].start);
        const bool cdda = lead_out || toc.tracks[row].type == svc::disc::TrackType::Cdda;
        const Msf m = msf_of(start + kPregapSectors);
        b[offs + 2] = cdda ? 0x00 : 0x04;
        row.number().store_byte(bytes.subspan(offs + kTrackByte).first<1>());
        b[offs + 5] = static_cast<std::uint8_t>(m.m);
        b[offs + 6] = static_cast<std::uint8_t>(m.s);
        b[offs + 7] = static_cast<std::uint8_t>(m.f);
    }

    lba_ = HeadLba{};
    hint_at_(lba_, geom);
    read_data_(geom);
    std::memcpy(b + kInfoTailOffset, sec_buf_, kInfoTailBytes);
    return {};
}

Ex<std::span<const std::byte>> ThreeDoCddEngine::do_disc_descriptor(
    std::span<std::byte, kMaxDescriptorBytes> out,
    const svc::disc::DiscGeometry& geom
) {
    if (auto r = build_disc_info(out, geom); !r)
        return std::unexpected(r.error());
    return std::span<const std::byte>(out);
}

Ex<void> ThreeDoCddEngine::do_push_sector(CdDialectPorts&) {

    return {};
}

void ThreeDoCddEngine::do_notify_disc(bool loaded, const svc::disc::DiscGeometry& geom) noexcept {

    loaded_ = loaded;

    if (loaded)
        hint_at_(HeadLba{}, geom);

    if (!loaded)
        do_reset_machine();
}

void ThreeDoCddEngine::do_reset_machine() noexcept {

    state_ = State::Open;
    read_pend_ = false;
    track_ = svc::TrackIndex{};
    lba_ = HeadLba{};
}

bool ThreeDoCddEngine::do_streaming(const svc::disc::DiscGeometry&) const noexcept {

    return read_pend_ || block_reads_ > SectorCount{};
}

CdEngine::DriveView ThreeDoCddEngine::do_drive_view(
    const svc::disc::DiscGeometry& geom
) const noexcept {
    const svc::disc::Toc& toc = geom.toc;
    const bool is_data = toc.last.contains(track_)
        && toc.tracks[track_].type != svc::disc::TrackType::Cdda;

    return DriveView{
        .state = drive_state_(),
        .track = track_,
        .lba = lba_,
        .audio_lba = lba_,
        .is_data = is_data
    };
}

CdDriveState ThreeDoCddEngine::drive_state_() const noexcept {
    switch (state_) {
        case State::Idle:
            return CdDriveState::Stopped;
        case State::Open:
            return CdDriveState::Open;
        case State::Read:
            return CdDriveState::Reading;
        case State::Pause:
            return CdDriveState::Paused;
        case State::Stop:
            return CdDriveState::Stopped;
    }
    return CdDriveState::Stopped;
}

}
