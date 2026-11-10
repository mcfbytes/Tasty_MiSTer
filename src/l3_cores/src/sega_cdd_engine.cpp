// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/sega_cdd_engine.h"

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>

#include "cd_core_detail.h"
#include "cores/cd_head_prime.h"
#include "cores/cd_msf.h"
#include "cores/cdwire.h"
#include "cores/sega_frame_word.h"
#include "infra/byte_view.h"
#include "infra/error.h"

namespace mister::cores {

namespace {

constexpr std::int32_t kCdScanSpeed = 30;

constexpr std::size_t kDataHeaderAt = 12;
constexpr std::size_t kDataHeaderBytes = 4;
constexpr std::size_t kUserDataAt = 16;

constexpr std::size_t kSubcodeFrameBytes = svc::kSubcodeBytes + 2;

constexpr std::int64_t kSeekTicksPerSpan = 120;
constexpr std::int64_t kSeekSpanSectors = 270000;

[[nodiscard]] std::uint32_t seek_latency_ticks(SectorCount distance) noexcept {
    return static_cast<std::uint32_t>(
        (std::int64_t{distance.magnitude().v} * kSeekTicksPerSpan) / kSeekSpanSectors
    );
}

}

SegaCddEngine::Comm SegaCddEngine::Comm::of(SegaFrameWord frame) noexcept {
    Comm out;
    out.opcode_ = detail::sega_command_of(frame.nibble(kOpcodeNibble));
    out.toc_submode_ = detail::toc_submode_of(frame.nibble(kTocSubmodeNibble));
    for (std::size_t slot = 0; slot < out.digits_.size(); ++slot) {
        const unsigned tens = frame.nibble(kBcd0TensNibble + 2 * slot);
        const unsigned ones = frame.nibble(kBcd0TensNibble + 2 * slot + 1);
        out.digits_[slot] = static_cast<std::uint8_t>(tens * 10u + ones);
    }
    return out;
}

void SegaCddEngine::do_notify_disc(bool loaded, const svc::DiscGeometry&) noexcept {

    st_.loaded = loaded;
    st_.status = loaded ? SegaCddStatus::Stop : SegaCddStatus::NoDisc;
    if (loaded)
        st_.latency = 10;
}

bool SegaCddEngine::do_streaming(const svc::DiscGeometry& geom) const noexcept {

    if (st_.status != SegaCddStatus::Play || st_.latency != 0)
        return false;
    const svc::Toc& toc = geom.toc;
    if (!toc.last.contains(st_.track))
        return false;
    const bool cdda = toc.at(st_.track).type == TrackType::Cdda;

    return row_.stream_on_data_track ? !cdda : cdda;
}

Ex<void> SegaCddEngine::do_push_sector(CdDialectPorts& p) {
    if (st_.status != SegaCddStatus::Play
        || st_.latency != 0
        || !p.geom.toc.last.contains(st_.track)) {
        return {};
    }
    return send_sector_(p);
}

Ex<void> SegaCddEngine::do_accept(std::span<const std::uint16_t> words, CdDialectPorts& p) {
    const std::uint16_t w0 = words[0], w1 = words[1], w2 = words[2];

    if (take_reset_request() || w0 == 0x00FFu)
        do_reset_machine();

    speed_index_ = static_cast<std::uint8_t>((w2 >> 8) & 0x3u);
    const std::uint64_t c = cdwire::sega_compose(w0, w1, w2);
    if (!cdwire::sega_command_ok(c, row_.crc_start)) {
        ++stats_.crc_faults;
    }
    command_exec_(Comm::of(SegaFrameWord{c}), p.geom);
    st_.has_command = true;
    return {};
}

Ex<void> SegaCddEngine::do_tick(CdDialectPorts& p) {

    if (st_.has_command) {
        const auto words = st_.stat.wire_words(
            st_.is_data,
            row_.crc_start,
            row_.status_carries_is_data
        );
        if (auto r = send_status_words_(words, p.link); !r)
            return r;
        st_.has_command = false;
    }
    return update_(p);
}

bool SegaCddEngine::can_send_ready_(TrackType type, bool skip_mode1, ICdLink& link) {

    if (skip_mode1 && type == TrackType::Mode1)
        return true;
    const auto selector = cdwire::sega_send_data_query(type, skip_mode1);

    if (!selector)
        return true;
    return link.can_send_data(*selector);
}

Ex<void> SegaCddEngine::update_(CdDialectPorts& p) {
    auto& m = st_;
    const svc::Toc& toc = p.geom.toc;

    if (m.status == SegaCddStatus::Stop
        || m.status == SegaCddStatus::Tray
        || m.status == SegaCddStatus::Open) {

        if (m.latency > 0) {
            --m.latency;
            return {};
        }
        if (!m.loaded)
            m.status = SegaCddStatus::NoDisc;
        return {};
    }
    if (m.status == SegaCddStatus::Seek) {
        if (m.latency > 0) {
            --m.latency;
            return {};
        }
        m.status = SegaCddStatus::Pause;
        return {};
    }
    if (m.status == SegaCddStatus::Play) {
        if (m.latency > 0) {
            --m.latency;
            return {};
        }
        if (!toc.last.contains(m.track)) {
            m.status = SegaCddStatus::End;
            return {};
        }
        return send_sector_(p);
    }
    if (m.status == SegaCddStatus::Scan) {
        m.lba += m.scan_offset;
        if (toc.last.contains(m.track) && m.lba >= head_of(toc.at(m.track).end)) {
            m.track += svc::TrackCount{1};
            if (toc.last.contains(m.track)) {
                m.lba = head_of(toc.at(m.track).start);
            } else {
                m.lba = head_of(toc.end);
                m.cdda.locate();
                m.status = SegaCddStatus::End;
                m.is_data = true;
                return {};
            }
        } else if (toc.last.contains(m.track) && m.lba < head_of(toc.at(m.track).start)) {
            if (m.track != svc::TrackIndex{}) {
                m.track -= svc::TrackCount{1};
                m.lba = head_of(toc.at(m.track).end);
            } else {
                m.lba = HeadLba{};
            }
        }
        m.cdda.locate();
        m.is_data = toc.last.contains(m.track) && toc.at(m.track).type != TrackType::Cdda;
        return {};
    }

    ++stats_.idle_ticks;
    return {};
}

Ex<void> SegaCddEngine::send_sector_(CdDialectPorts& p) {

    auto& m = st_;
    const svc::Toc& toc = p.geom.toc;
    const svc::Track& tk = toc.at(m.track);
    const bool data_track = tk.type != TrackType::Cdda;

    pump_pipeline_(p.geom);

    if (p.egress_busy()) {
        ++stats_.busy_ticks;
        return {};
    }

    if (!can_send_ready_(tk.type, row_.ready_probe_skips_mode1, p.link)) {
        ++stats_.flow_waits;
        return {};
    }

    std::size_t len = svc::kRawFrameBytes;
    IoIndex idx = io_.data;
    if (data_track) {

        const Msf msf = msf_of(m.lba + kPregapSectors);
        const std::span<std::uint8_t> header = std::span(sec_buf_).subspan(
            kDataHeaderAt,
            kDataHeaderBytes
        );
        header[0] = bcd(msf.m);
        header[1] = bcd(msf.s);
        header[2] = bcd(msf.f);
        header[3] = 0x01;
        if (const std::optional<Lba> at = m.lba.readable()) {

            const svc::DiscTake got = facts_.disc.take(
                svc::DiscAsk{.form = svc::DiscForm::UserData, .lba = *at},
                std::as_writable_bytes(
                    std::span(sec_buf_).subspan(kUserDataAt, svc::kUserDataBytes)
                )
            );
            if (!got.is_bytes()) {
                ++stats_.substitutes;
            }
            facts_.disc.hint(
                svc::DiscAsk{
                    .form = svc::DiscForm::UserData,
                    .lba = *(m.lba + SectorCount{1}).readable()
                }
            );
        }
    } else {
        if (m.lba >= head_of(tk.start)) {
            m.is_data = false;
        }

        const SectorCount sectors = m.cdda.frame_sectors();
        len = sectors.bytes(SectorBytes::of(svc::kRawFrameBytes)).length_of(proto::BlockCount{1});
        idx = io_.cdda;
        if (!m.is_data) {
            const HeadLba from = m.cdda.at(m.lba);
            for (SectorCount i{}; i < sectors; i += SectorCount{1}) {
                const auto read_lba = (from + i).readable();
                if (read_lba) {
                    const svc::DiscTake got = facts_.disc.take(
                        svc::DiscAsk{.form = svc::DiscForm::RawFrame, .lba = *read_lba},
                        std::as_writable_bytes(
                            std::span(sec_buf_).subspan(
                                i.bytes(SectorBytes::of(svc::kRawFrameBytes))
                                    .length_of(proto::BlockCount{1}),
                                svc::kRawFrameBytes
                            )
                        )
                    );
                    if (!got.is_bytes()) {
                        ++stats_.substitutes;
                    }
                }
            }
            m.cdda.read(sectors);
        }
    }

    const std::span<const std::uint8_t> sub = subcode_stage_();
    const std::span<const std::uint8_t> sector = std::span<const std::uint8_t>(sec_buf_).first(len);
    if (p.link.egress_bound()) {
        if (auto r = p.link.submit_sector({io_.subcode, sub, idx, sector}); !r)
            return r;
    } else {
        if (!sub.empty()) {
            if (auto r = send_frame(p.link, io_.subcode, sub); !r)
                return r;
        }
        if (auto r = send_frame(p.link, idx, sector); !r)
            return r;
    }

    if (!data_track) {
        stats_.cdda_sectors += static_cast<std::uint32_t>(len / svc::kRawFrameBytes);
    } else {
        ++stats_.data_sectors;
    }

    m.lba += SectorCount{1};
    if (toc.last.contains(m.track) && m.lba >= head_of(toc.at(m.track).end)) {
        m.track += svc::TrackCount{1};
        m.is_data = true;
    }

    pump_pipeline_(p.geom);
    return {};
}

std::span<const std::uint8_t> SegaCddEngine::subcode_stage_() {

    auto& m = st_;
    const std::optional<Lba> sub_at = m.lba.readable();
    if (!sub_at)
        return {};
    const svc::DiscTake sub_got = facts_.disc.take(
        svc::DiscAsk{.form = svc::DiscForm::Subcode, .lba = *sub_at},
        std::as_writable_bytes(std::span(sub_buf_).first(svc::kSubcodeBytes))
    );
    facts_.disc.hint(
        svc::DiscAsk{.form = svc::DiscForm::Subcode, .lba = *(m.lba + SectorCount{1}).readable()}
    );
    if (!sub_got.is_bytes()) {
        ++stats_.subcode_substitutes;
        return {};
    }
    return std::span<const std::uint8_t>(sub_buf_).first(kSubcodeFrameBytes);
}

void SegaCddEngine::hint_at_(HeadLba lba, const svc::DiscGeometry& geom) noexcept {
    const std::optional<Lba> at = lba.readable();
    if (!at)
        return;
    const svc::Toc& toc = geom.toc;
    const svc::TrackIndex tr = track_scan(toc, lba);
    const bool data = toc.last.contains(tr) && toc.at(tr).type != TrackType::Cdda;

    prime_head(
        facts_.disc,
        *at,
        {.form = data ? svc::DiscForm::UserData : svc::DiscForm::RawFrame,
         .pair = !data,
         .subcode = true}
    );
}

void SegaCddEngine::pump_pipeline_(const svc::DiscGeometry& geom) noexcept {
    auto& m = st_;
    const svc::Toc& toc = geom.toc;
    const std::optional<Lba> head = m.lba.readable();
    if (toc.last == svc::TrackCount{} || !head)
        return;
    const svc::TrackIndex tr = track_scan(toc, m.lba);
    if (toc.last.contains(tr) && toc.at(tr).type != TrackType::Cdda) {
        facts_.disc.hint(svc::DiscAsk{.form = svc::DiscForm::UserData, .lba = *head});
    } else if (const std::optional<Lba> from = m.cdda.at(m.lba).readable()) {
        facts_.disc.hint(svc::DiscAsk{.form = svc::DiscForm::RawFrame, .lba = *from});
        if (const std::optional<Lba> next = m.cdda.at(m.lba + SectorCount{1}).readable()) {
            facts_.disc.hint(svc::DiscAsk{.form = svc::DiscForm::RawFrame, .lba = *next});
        }
    }
    facts_.disc.hint(svc::DiscAsk{.form = svc::DiscForm::Subcode, .lba = *head});
}

void SegaCddEngine::seek_(HeadLba lba, bool play, const svc::DiscGeometry& geom) {

    auto& m = st_;
    const svc::Toc& toc = geom.toc;
    m.latency = (play ? 11u : 0u) + seek_latency_ticks(lba - m.lba);
    m.lba = lba;
    m.track = track_scan(toc, lba);
    hint_at_(lba, geom);

    m.cdda.locate();
    if (play)
        m.cdda.reprime();
}

void SegaCddEngine::do_reset_machine() noexcept {

    auto& m = st_;
    m.latency = 10;
    m.track = svc::TrackIndex{};
    m.lba = HeadLba{};
    m.cdda.locate();
    m.cdda.reprime();
    m.scan_offset = SectorCount{};
    m.is_data = true;
    m.status = SegaCddStatus::Stop;
    m.stat = SegaCddStat{};
}

void SegaCddEngine::command_exec_(const Comm& frame, const svc::DiscGeometry& geom) {

    auto& m = st_;
    const svc::Toc& toc = geom.toc;
    using Field = SegaCddStat::Field;
    const auto cur_type_nib = [&]() -> std::uint8_t {
        return toc.last.contains(m.track) ? static_cast<std::uint8_t>(toc.at(m.track).type) : 0u;
    };
    const auto write_msf = [&](HeadLba lba) {
        m.stat.set_msf(msf_of(lba));
    };
    const auto write_absent_track = [&]() {
        m.stat.set(Field::Bcd0Tens, SegaCddStat::kAbsent);
        m.stat.set(Field::Bcd0Ones, SegaCddStat::kAbsent);
    };
    const auto write_track = [&]() {
        if (toc.last.contains(m.track)) {
            m.stat.set_bcd(SegaCddStat::BcdSlot::Bcd0, m.track.number());
        } else {
            write_absent_track();
        }
    };

    const std::optional<SegaCddCommand> command = frame.opcode();
    if (!command) {
        m.stat.set_status(m.status);
        return;
    }

    switch (*command) {
        case SegaCddCommand::Idle:

            if (m.latency <= 3) {
                m.stat.set_status(m.status);
                if (m.stat.subcommand() == 0x0F) {
                    m.stat.set_subcommand(0x0);
                    write_msf(m.lba + kPregapSectors);
                    m.stat.set_type_byte(cur_type_nib() != 0 ? 0x04 : 0x00);
                } else if (m.stat.subcommand() == 0x00) {
                    write_msf(m.lba + kPregapSectors);
                    m.stat.set_type_byte(cur_type_nib() != 0 ? 0x04 : 0x00);
                } else if (m.stat.subcommand() == 0x01) {
                    const HeadLba base = toc.last.contains(m.track) ? head_of(toc.at(m.track).start)
                                                                    : HeadLba{};
                    write_msf(HeadLba{} + (m.lba - base).magnitude());
                    m.stat.set_type_byte(cur_type_nib() != 0 ? 0x04 : 0x00);
                } else if (m.stat.subcommand() == 0x02) {
                    write_track();
                }
            }
            break;

        case SegaCddCommand::Stop:
            m.status = SegaCddStatus::Stop;
            m.is_data = true;
            m.stat.set_status(m.status);
            m.stat.clear_from(Field::Subcommand);
            break;

        case SegaCddCommand::Toc: {
            if (m.status == SegaCddStatus::Stop)
                m.status = SegaCddStatus::Toc;
            switch (frame.toc_submode()) {
                case TocSubmode::Position:
                    m.stat.set_status(m.status);
                    m.stat.set_subcommand(0x0);
                    write_msf(m.lba + kPregapSectors);
                    m.stat.set_type_byte(static_cast<std::uint8_t>(cur_type_nib() << 2));
                    break;
                case TocSubmode::TrackRelative: {
                    const HeadLba base = toc.last.contains(m.track) ? head_of(toc.at(m.track).start)
                                                                    : HeadLba{};
                    m.stat.set_status(m.status);
                    m.stat.set_subcommand(0x1);
                    write_msf(HeadLba{} + (m.lba - base).magnitude());
                    m.stat.set_type_byte(static_cast<std::uint8_t>(cur_type_nib() << 2));
                    break;
                }
                case TocSubmode::TrackNumber:
                    m.stat.set_status(m.status);
                    m.stat.set_subcommand(0x2);
                    write_track();
                    m.stat.clear_from(Field::Bcd1Tens);
                    break;
                case TocSubmode::LeadOut:
                    m.stat.set_status(m.status);
                    m.stat.set_subcommand(0x3);
                    write_msf(head_of(toc.end) + kPregapSectors);
                    m.stat.set_type_byte(0);
                    break;
                case TocSubmode::FirstLast:
                    m.stat.set_status(m.status);
                    m.stat.set_subcommand(0x4);
                    m.stat.set(Field::Bcd0Tens, 0);
                    m.stat.set(Field::Bcd0Ones, 1);
                    m.stat.set_bcd(SegaCddStat::BcdSlot::Bcd1, toc.last);
                    m.stat.clear_from(Field::Bcd2Tens);
                    break;
                case TocSubmode::TrackInfo: {

                    const svc::TrackNumber track = toc.last.clamped_number(
                        svc::CueField{frame.digits(SegaCddStat::BcdSlot::Bcd1)}
                    );
                    const svc::Track& tk = toc.at(track.index());
                    const Msf msf = msf_of(head_of(tk.start) + kPregapSectors);
                    m.stat.set_status(m.status);
                    m.stat.set_subcommand(0x5);
                    m.stat.set_bcd(SegaCddStat::BcdSlot::Bcd0, msf.m);
                    m.stat.set_bcd(SegaCddStat::BcdSlot::Bcd1, msf.s);
                    const std::uint8_t f_packed = cdwire::bcd(msf.f);
                    m.stat.set(
                        Field::Bcd2Tens,
                        static_cast<std::uint8_t>(
                            (f_packed >> 4) | (static_cast<std::uint8_t>(tk.type) << 3)
                        )
                    );
                    m.stat.set(
                        Field::Bcd2Ones,
                        static_cast<std::uint8_t>(f_packed & SegaCddStat::kNibbleMask)
                    );
                    m.stat.set(
                        Field::Type,
                        static_cast<std::uint8_t>(cdwire::bcd(track) & SegaCddStat::kNibbleMask)
                    );
                    break;
                }
                case TocSubmode::SubQ:
                    m.stat.set_status(m.status);
                    m.stat.set_subcommand(0x6);
                    m.stat.clear_from(Field::Bcd0Tens);
                    break;
            }
            break;
        }

        case SegaCddCommand::Play:
        case SegaCddCommand::Seek: {

            const Msf msf = frame.msf();
            const HeadLba lba = HeadLba::of(msf) - kPregapSectors;
            const bool play = *command == SegaCddCommand::Play;
            seek_(lba, play, geom);
            m.is_data = true;
            m.status = play ? SegaCddStatus::Play : SegaCddStatus::Seek;
            m.stat.set_status(SegaCddStatus::Seek);
            m.stat.set_subcommand(0xF);
            m.stat.clear_from(Field::Bcd0Tens);
            break;
        }

        case SegaCddCommand::Pause:
            m.is_data = true;
            m.status = SegaCddStatus::Pause;
            m.stat.set_status(m.status);
            break;

        case SegaCddCommand::Resume:
            m.status = SegaCddStatus::Play;
            m.stat.set_status(m.status);
            m.cdda.reprime();
            break;

        case SegaCddCommand::FwScan:
            m.scan_offset = SectorCount{kCdScanSpeed};
            m.status = SegaCddStatus::Scan;
            m.stat.set_status(m.status);
            break;

        case SegaCddCommand::RwScan:
            m.scan_offset = SectorCount{-kCdScanSpeed};
            m.status = SegaCddStatus::Scan;
            m.stat.set_status(m.status);
            break;

        case SegaCddCommand::TrackMove:

            m.is_data = true;
            m.status = SegaCddStatus::Pause;
            m.stat.set_status(m.status);
            break;

        case SegaCddCommand::TrackPlay: {

            const svc::TrackIndex
                row = toc.last
                          .clamped_number(svc::CueField{frame.digits(SegaCddStat::BcdSlot::Bcd0)})
                          .index();
            const HeadLba lba = head_of(toc.at(row).start);
            seek_(lba, true, geom);
            m.is_data = true;
            m.status = SegaCddStatus::Play;
            m.stat.set_status(SegaCddStatus::Seek);
            m.stat.set_subcommand(0xF);
            m.stat.clear_from(Field::Bcd0Tens);
            break;
        }

        case SegaCddCommand::TrayClose:
            m.is_data = true;
            m.status = m.loaded ? SegaCddStatus::Toc : SegaCddStatus::NoDisc;
            m.stat.set_status(SegaCddStatus::Stop);
            break;

        case SegaCddCommand::TrayOpen:
            m.is_data = true;
            m.status = SegaCddStatus::Open;
            m.stat.set_status(SegaCddStatus::Open);
            break;
    }
}

}
