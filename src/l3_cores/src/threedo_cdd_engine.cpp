// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/threedo_cdd_engine.h"

#include <cstring>

#include "cores/cd_head_prime.h"
#include "cores/cdwire.h"
#include "infra/byte_view.h"

namespace mister::cores {

namespace {

using cdwire::bcd;
using cdwire::lba_to_msf;
using cdwire::Msf;

constexpr std::uint8_t kCommNop = 0x00;
constexpr std::uint8_t kCommRead = 0x10;
constexpr std::uint8_t kCommNext = 0x20;
constexpr std::uint8_t kDiscDaOrCdrom = 0x00;

constexpr std::size_t kUserData = 2048;
constexpr std::size_t kRawFrame = 2352;
constexpr std::size_t kHeaderBytes = 16;
constexpr std::size_t kChecksumBytes = 2;
constexpr std::size_t kInfoTailBytes = 128;
constexpr std::size_t kInfoTailOffset = 1024;
constexpr std::int32_t kRedBookPregap = 150;

constexpr std::int32_t lba_from_command(std::span<const std::uint8_t, 8> c) noexcept {
    const std::int32_t raw = (static_cast<std::int32_t>(c[1]) << 16)
        | (static_cast<std::int32_t>(c[2]) << 8)
        | static_cast<std::int32_t>(c[3]);
    if (c[4] != 0)
        return raw;
    return static_cast<std::int32_t>(c[1]) * 60 * 75
        + static_cast<std::int32_t>(c[2]) * 75
        + static_cast<std::int32_t>(c[3]);
}

constexpr std::int32_t blocks_from_command(std::span<const std::uint8_t, 8> c) noexcept {
    return (static_cast<std::int32_t>(c[5]) << 8) | static_cast<std::int32_t>(c[6]);
}

}

Ex<void> ThreeDoCddEngine::do_accept(std::span<const std::uint16_t> words) {

    for (std::size_t i = 0; i < 4; ++i) {
        comm_[2 * i] = static_cast<std::uint8_t>(words[i] & 0xFFu);
        comm_[2 * i + 1] = static_cast<std::uint8_t>(words[i] >> 8);
    }
    command_exec_();
    return {};
}

void ThreeDoCddEngine::command_exec_() {
    const std::span<const std::uint8_t, 8> c{comm_};
    const std::int32_t cmd_lba = lba_from_command(c);
    const std::int32_t cmd_blocks = blocks_from_command(c);
    switch (comm_[0]) {
        case kCommNop:
            break;
        case kCommRead:
            lba_ = cmd_lba - kRedBookPregap;
            hint_at_(lba_);
            track_ = track_by_lba_(lba_);
            read_pend_ = true;
            block_reads_ = cmd_blocks;
            break;
        case kCommNext:

            if (block_reads_ > 0)
                read_pend_ = true;
            break;
        default:
            read_pend_ = false;
            break;
    }
}

Ex<void> ThreeDoCddEngine::do_tick() {

    if (read_pend_ && ports_.egress_busy()) {
        ++stats_.busy_ticks;
        return ports_.tx.send_status(0);
    }
    if (read_pend_) {
        state_ = State::Read;
        read_pend_ = false;
    } else {
        state_ = State::Idle;
    }
    if (auto r = ports_.tx.send_status(0); !r)
        return r;
    return update_();
}

Ex<void> ThreeDoCddEngine::update_() {
    const svc::Toc& toc = ports_.geom.toc;
    if (state_ != State::Read) {

        if (state_ != State::Open)
            track_ = track_by_lba_(lba_);
        ++stats_.idle_ticks;
        return {};
    }

    Ex<void> sent{};
    if (track_ < toc.last && toc.tracks[track_].type == svc::TrackType::Mode1) {
        const svc::Track& tk = toc.tracks[track_];

        if (toc.sector_size == kUserData || lba_ < static_cast<std::int32_t>(tk.start.v)) {
            const Msf msf = lba_to_msf(lba_ + kRedBookPregap);
            std::uint8_t header[kHeaderBytes];
            header[0] = 0x00;
            for (std::size_t i = 1; i <= 10; ++i)
                header[i] = 0xFF;
            header[11] = 0x00;
            header[12] = bcd(msf.m);
            header[13] = bcd(msf.s);
            header[14] = bcd(msf.f);
            header[15] = static_cast<std::uint8_t>(tk.type != svc::TrackType::Cdda);
            sent = data_sector_send_(std::span<const std::uint8_t>(header));
        } else {
            sent = data_sector_send_({});
        }
    }

    ++lba_;
    track_ = track_by_lba_(lba_);
    --block_reads_;
    return sent;
}

Ex<void> ThreeDoCddEngine::data_sector_send_(std::span<const std::uint8_t> header) {

    read_data_();
    if (!header.empty())
        std::memcpy(sec_buf_, header.data(), header.size());

    std::uint16_t crc = 0;
    for (std::size_t i = 0; i < kUserData; ++i) {
        crc = static_cast<std::uint16_t>(crc + sec_buf_[kHeaderBytes + i]);
    }
    sec_buf_[kHeaderBytes + kUserData] = static_cast<std::uint8_t>(crc & 0xFFu);
    sec_buf_[kHeaderBytes + kUserData + 1] = static_cast<std::uint8_t>(crc >> 8);

    ++stats_.data_sectors;
    return ports_.tx.send_data(
        ports_.profile.io.data,
        std::as_bytes(
            std::span<const std::uint8_t>(sec_buf_ + kHeaderBytes, kUserData + kChecksumBytes)
        )
    );
}

void ThreeDoCddEngine::hint_at_(std::int32_t lba) noexcept {
    if (lba < 0)
        return;
    const svc::Toc& toc = ports_.geom.toc;

    prime_head(
        ports_.disc,
        lba,
        {.form = toc.sector_size == kUserData ? svc::DiscForm::UserData : svc::DiscForm::RawFrame}
    );
}

void ThreeDoCddEngine::read_data_() {

    const svc::Toc& toc = ports_.geom.toc;
    if (track_ >= toc.last || toc.tracks[track_].type != svc::TrackType::Mode1)
        return;
    const svc::Lba at{static_cast<std::uint32_t>(lba_ > 0 ? lba_ : 0)};

    const bool got = toc.sector_size == kUserData
        ? ports_.disc.take(
              svc::DiscForm::UserData,
              at,
              std::as_writable_bytes(std::span<std::uint8_t>(sec_buf_ + kHeaderBytes, kUserData))
          ) != 0
        : ports_.disc.take(
              svc::DiscForm::RawFrame,
              at,
              std::as_writable_bytes(std::span<std::uint8_t>(sec_buf_, kRawFrame))
          ) != 0;
    if (!got)
        ++stats_.substitutes;
    ports_.disc.hint(
        toc.sector_size == kUserData ? svc::DiscForm::UserData : svc::DiscForm::RawFrame,
        svc::Lba{at.v + 1}
    );
}

std::uint32_t ThreeDoCddEngine::track_by_lba_(std::int32_t lba) const noexcept {

    const svc::Toc& toc = ports_.geom.toc;
    std::uint32_t i = 0;
    while (i < toc.last && static_cast<std::int64_t>(toc.tracks[i].end.v) <= lba)
        ++i;
    return i;
}

Ex<void> ThreeDoCddEngine::build_disc_info(std::span<std::byte, kDiscInfoBytes> out) {

    if (!ports_.geom.mounted)
        return std::unexpected(Error{Errc::core_load, ERR_SITE(), 0});

    const svc::Toc& toc = ports_.geom.toc;
    std::memset(out.data(), 0x00, out.size());
    auto* b = infra::byte_cast<std::uint8_t>(out).data();

    const Msf end = lba_to_msf(static_cast<std::int32_t>(toc.end.v) + kRedBookPregap);
    b[1] = kDiscDaOrCdrom;
    b[2] = 0x01;
    b[3] = toc.last;
    b[4] = static_cast<std::uint8_t>(end.m);
    b[5] = static_cast<std::uint8_t>(end.s);
    b[6] = static_cast<std::uint8_t>(end.f);

    for (std::uint32_t i = 0, offs = 0x10; i <= toc.last; ++i, offs += 0x10) {

        const bool lead_out = i >= toc.last;
        const std::uint32_t start = lead_out ? toc.end.v : toc.tracks[i].start.v;
        const bool cdda = lead_out || toc.tracks[i].type == svc::TrackType::Cdda;
        const Msf m = lba_to_msf(static_cast<std::int32_t>(start) + kRedBookPregap);
        b[offs + 2] = cdda ? 0x00 : 0x04;
        b[offs + 3] = static_cast<std::uint8_t>(i + 1);
        b[offs + 5] = static_cast<std::uint8_t>(m.m);
        b[offs + 6] = static_cast<std::uint8_t>(m.s);
        b[offs + 7] = static_cast<std::uint8_t>(m.f);
    }

    lba_ = 0;
    hint_at_(lba_);
    read_data_();
    std::memcpy(b + kInfoTailOffset, sec_buf_, kInfoTailBytes);
    return {};
}

Ex<std::size_t> ThreeDoCddEngine::do_disc_descriptor(std::span<std::byte> out) {
    if (out.size() < kDiscInfoBytes) {
        return std::unexpected(Error{Errc::core_load, ERR_SITE(), 1});
    }
    if (auto r = build_disc_info(out.subspan<0, kDiscInfoBytes>()); !r) {
        return std::unexpected(r.error());
    }
    return kDiscInfoBytes;
}

Ex<void> ThreeDoCddEngine::do_push_sector() {

    return {};
}

Ex<void> ThreeDoCddEngine::do_notify_disc(bool loaded) {

    loaded_ = loaded;

    if (loaded)
        hint_at_(0);

    if (!loaded)
        do_reset_machine();
    return {};
}

void ThreeDoCddEngine::do_reset_machine() noexcept {

    state_ = State::Open;
    read_pend_ = false;
    track_ = 0;
    lba_ = 0;
}

bool ThreeDoCddEngine::do_streaming() const noexcept {

    return read_pend_ || block_reads_ > 0;
}

CdEngine::DriveView ThreeDoCddEngine::do_drive_view() const noexcept {
    const svc::Toc& toc = ports_.geom.toc;
    const bool is_data = track_ < toc.last && toc.tracks[track_].type != svc::TrackType::Cdda;

    return DriveView{static_cast<std::uint8_t>(state_), track_, lba_, lba_, is_data};
}

std::size_t ThreeDoCddEngine::do_status_words(
    std::uint64_t,
    std::span<std::uint16_t, kMaxStatusWords> out
) const noexcept {

    out[0] = 0;
    out[1] = 0;
    out[2] = 0;
    out[3] = 0;
    return 4;
}

}
