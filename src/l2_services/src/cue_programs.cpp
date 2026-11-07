// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstring>
#include <memory>
#include <string>

#include "disc_lex.h"
#include "svc/index_pair_cue_program.h"
#include "svc/index_triple_cue_program.h"
#include "svc/sentinel_cue_program.h"

namespace mister::svc {

using namespace disc::detail;

namespace {

constexpr std::string_view kMode1Cooked = "MODE1/2048";
constexpr std::string_view kMode1Raw = "MODE1/2352";
constexpr std::string_view kMode2Raw = "MODE2/2352";

constexpr OpenFileId kNoBacking{kMaxTracks};

constexpr std::string_view kSegaMagic = "SEGADISCSYSTEM";

}

Ex<void> SentinelCueProgram::parse(std::string_view cue_path, const CueMount& m) const {

    m.cdda_order = CddaOrder::LittleEndian;

    auto text_r = m.io.read_cue_text(cue_path);
    if (!text_r)
        return std::unexpected(text_r.error());
    const std::string text = std::move(*text_r);

    const std::string_view base_dir = dir_of(cue_path);

    TrackCount last{};
    std::uint32_t pregap = 0;

    std::uint32_t track_pregap = 0;
    std::uint32_t hdr = 0;
    std::string fname;
    PendingFile pending;
    OpenFileId last_backing{};
    bool track0_size_stated = false;
    bool aborted = false;

    std::uint32_t running_ss = 0;
    const bool pregap_relative = policy_.track_close
        == CuePolicy::TrackClose::SentinelPregapRelative;

    LineReader reader(text);
    char line[1024];
    while (reader.next(std::span(line))) {
        std::string_view sv = lstrip(std::string_view(line));

        if (sv.starts_with("FILE")) {
            const CueFileDecl decl = take_cue_file(lstrip(past(sv, kKwFile)));
            const std::string_view rest = decl.format;

            fname.assign(base_dir);
            fname.append(decl.name);

            auto f = m.io.open_file(fname);
            if (!f)
                return std::unexpected(f.error());
            auto sz = (*f)->size();
            if (!sz)
                return std::unexpected(sz.error());

            if (rest.find("BINARY") == std::string_view::npos
                && rest.find("MOTOROLA") == std::string_view::npos
                && rest.find("WAVE") == std::string_view::npos) {
                return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
            }

            pending.adopt(std::move(*f), *sz);

            constexpr std::uint32_t kWavHeaderBytes = 44;
            hdr = (policy_.wav_header_skip && ends_with_ci(fname, ".wav")) ? kWavHeaderBytes : 0u;
            pregap = 0;

            track_pregap = 0;
            m.toc.at(last.next_index()).offset = 0;
            continue;
        }

        if (sv.starts_with("TRACK")) {
            std::string_view rest = past(sv, kKwTrack);
            const auto number = take_uint(rest, kCueFieldDigits);
            if (!number)
                continue;

            if (!last.next_index().number().matches(*number)) {
                aborted = true;
                break;
            }

            track_pregap = 0;
            Track& t = m.toc.at(last.next_index());
            if (policy_.type_detect == CuePolicy::TypeDetect::Track0Only) {

                if (last.empty()) {
                    if (sv.find(kMode1Cooked) != std::string_view::npos) {
                        t.sector_size = kUserDataBytes;
                        t.type = TrackType::Mode1;
                        track0_size_stated = true;
                    } else if (sv.find(kMode1Raw) != std::string_view::npos) {
                        t.sector_size = kCdDataSize;
                        t.type = TrackType::Mode1;
                        track0_size_stated = true;
                    }
                }
            } else if (policy_.type_detect == CuePolicy::TypeDetect::PerTrackRunningSize) {

                if (sv.find(kMode1Cooked) != std::string_view::npos) {
                    running_ss = kUserDataBytes;
                    t.type = TrackType::Mode1;
                } else if (sv.find(kMode1Raw) != std::string_view::npos) {
                    running_ss = kCdDataSize;
                    t.type = TrackType::Mode1;
                } else if (sv.find(kMode2Raw) != std::string_view::npos) {
                    running_ss = kCdDataSize;
                    t.type = TrackType::Mode2;
                }
                if (t.type != TrackType::Cdda)
                    t.sector_size = running_ss;
            } else {

                if (sv.find(kMode1Cooked) != std::string_view::npos) {
                    t.sector_size = kUserDataBytes;
                    t.type = TrackType::Mode1;
                    if (last.empty())
                        track0_size_stated = true;
                } else if (sv.find(kMode1Raw) != std::string_view::npos) {
                    t.sector_size = kCdDataSize;
                    t.type = TrackType::Mode1;
                    if (last.empty())
                        track0_size_stated = true;
                } else if (sv.find("AUDIO") != std::string_view::npos) {
                    t.sector_size = kCdDataSize;
                    t.type = TrackType::Cdda;
                }
            }

            if (!last.empty() && !pending.open)
                m.toc.at(last.last_index()).end = Lba{0};
            continue;
        }

        if (sv.starts_with("PREGAP")) {
            if (const auto frames = take_msf(past(sv, kKwPregap))) {
                pregap += *frames;
                track_pregap = *frames;
            }
            continue;
        }

        if (sv.starts_with("INDEX")) {
            std::string_view rest = past(sv, kKwIndex);
            const auto index = take_uint(rest, kCueFieldDigits);
            if (!index)
                continue;
            const auto frames = take_msf(rest);
            if (!frames)
                continue;

            if (*index == 0) {

                if (!last.empty() && m.toc.at(last.last_index()).end == Lba{0}) {
                    m.toc.at(last.last_index()).end = pregap_relative
                        ? Lba{0} + BlockCount{pregap}
                        : Lba{0} + BlockCount{*frames + pregap};
                }
                continue;
            }
            if (*index != 1)
                continue;

            Track& t = m.toc.at(last.next_index());
            t.number = last.next_index().number();

            const bool accumulate = (policy_.offset_op == CuePolicy::OffsetOp::Accumulate);
            if (accumulate) {
                t.offset += std::int64_t{pregap} * std::int64_t{kCdDataSize};
            }

            if (!pending.open) {

                if (m.table.count() == 0) {
                    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
                }
                t.backing = last_backing;
                t.start = Lba{0} + BlockCount{*frames + pregap};
                if (!accumulate) {
                    t.offset = std::int64_t{pregap} * std::int64_t{t.sector_size}
                        - std::int64_t{hdr};
                }
                if (!last.empty() && m.toc.at(last.last_index()).end == Lba{0}) {
                    m.toc.at(last.last_index()).end = pregap_relative
                        ? t.start - BlockCount{track_pregap}
                        : t.start;
                }
            } else {

                if (m.table.count() >= kMaxTracks) {
                    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), m.table.count()});
                }
                last_backing = m.table.push(pending.take());
                t.backing = last_backing;

                t.start = m.toc.end + BlockCount{pregap};

                std::uint32_t ss = t.sector_size;
                if (policy_.type_detect == CuePolicy::TypeDetect::Track0Only) {
                    ss = (t.type != TrackType::Cdda) ? m.toc.at(TrackIndex{}).sector_size
                                                     : kCdDataSize;
                }
                if (ss == 0)
                    ss = kCdDataSize;

                if (accumulate) {
                    t.offset += std::int64_t{m.toc.end.v} * std::int64_t{kCdDataSize};
                } else {
                    t.offset = std::int64_t{t.start.v} * std::int64_t{ss} - std::int64_t{hdr};
                }

                const proto::FileSize payload = pending.size.after_header(proto::FileSize{hdr});
                t.end = t.start + payload.units_covering(proto::FileSize{ss});
                t.start += BlockCount{*frames};
                m.toc.end = t.end;
            }

            last += TrackCount{1};
            if (last == kMaxCueTracks)
                break;
            continue;
        }
    }

    if (aborted) {
        return std::unexpected(last.refusal(Errc::bad_format, ERR_SITE()));
    }

    if (!last.empty() && m.toc.at(last.last_index()).end == Lba{0}) {
        m.toc.end += BlockCount{pregap};
        m.toc.at(last.last_index()).end = m.toc.end;
    }
    if (last.empty()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    if (policy_.type_detect == CuePolicy::TypeDetect::PerTrackRunningSize) {

        m.toc.sector_size = (running_ss != 0) ? running_ss : kCdDataSize;
    } else if (track0_size_stated) {
        m.toc.sector_size = m.toc.at(TrackIndex{}).sector_size;
    } else if (policy_.sniff_sega_disc_system && m.table.holds(m.toc.at(TrackIndex{}).backing)) {
        std::byte head[16]{};
        IFile* f = m.table.file(m.toc.at(TrackIndex{}).backing);
        m.toc.sector_size = kCdDataSize;
        if (f != nullptr) {
            auto rd = f->read_at(0, std::span<std::byte>(head, sizeof(head)));
            if (rd
                && *rd == sizeof(head)
                && std::memcmp(head, kSegaMagic.data(), kSegaMagic.size()) == 0) {
                m.toc.sector_size = kUserDataBytes;
            }
        }
    } else {
        m.toc.sector_size = kCdDataSize;
    }

    if (policy_.type_detect != CuePolicy::TypeDetect::PerTrack) {
        last.for_each_index([&](TrackIndex i) {
            Track& row = m.toc.at(i);
            if (row.type != TrackType::Cdda)
                row.sector_size = m.toc.sector_size;
        });
    }

    close_toc(m.toc, last);

    std::string sub_name;
    if (policy_.sub_naming == CuePolicy::SubNaming::LastTrackFilename
        && fname.size() > kExtSub.size()) {
        sub_name.assign(fname, 0, fname.size() - kExtSub.size());
    }
    if (!sub_name.empty()) {
        sub_name.append(".sub");
        auto s = m.io.open_file(sub_name);
        if (s) {
            m.subcode.attach(std::move(*s));
            m.toc.has_subcode = true;
        }
    }
    return {};
}

Ex<void> IndexTripleCueProgram::parse(std::string_view cue_path, const CueMount& m) const {
    m.cdda_order = CddaOrder::LittleEndian;

    auto text_r = m.io.read_cue_text(cue_path);
    if (!text_r)
        return std::unexpected(text_r.error());
    const std::string text = std::move(*text_r);

    const std::string_view base_dir = dir_of(cue_path);

    TrackCount last{};

    std::uint32_t pregap = 0;
    std::string fname;
    PendingFile pending;

    LineReader reader(text);
    char line[1024];
    while (reader.next(std::span(line))) {
        std::string_view sv = lstrip(std::string_view(line));

        if (sv.starts_with("FILE")) {
            const CueFileDecl decl = take_cue_file(lstrip(past(sv, kKwFile)));
            const std::string_view rest = decl.format;
            fname.assign(base_dir);
            fname.append(decl.name);

            auto f = m.io.open_file(fname);
            if (!f)
                return std::unexpected(f.error());
            auto sz = (*f)->size();
            if (!sz)
                return std::unexpected(sz.error());

            if (rest.find("BINARY") == std::string_view::npos) {
                return std::unexpected(last.refusal(Errc::bad_format, ERR_SITE()));
            }
            pending.adopt(std::move(*f), *sz);
            continue;
        }

        if (sv.starts_with("TRACK")) {
            std::string_view rest = past(sv, kKwTrack);
            const auto number = take_uint(rest, kCueFieldDigits);
            if (!number)
                continue;
            pregap = 0;

            if (!last.next_index().number().matches(*number)) {
                return std::unexpected(last.refusal(Errc::bad_format, ERR_SITE()));
            }
            Track& t = m.toc.at(last.next_index());
            if (sv.find(kMode1Raw) != std::string_view::npos
                || sv.find(kMode2Raw) != std::string_view::npos) {
                t.sector_size = kCdDataSize;
                t.type = TrackType::Mode1;

                if (last.empty())
                    m.toc.end = Lba{0} + kRedBookPregap;
            } else if (sv.find("AUDIO") != std::string_view::npos) {
                t.sector_size = kCdDataSize;
                t.type = TrackType::Cdda;
            } else {

                return std::unexpected(last.refusal(Errc::bad_format, ERR_SITE()));
            }
            continue;
        }

        if (sv.starts_with("PREGAP")) {
            if (const auto frames = take_msf(past(sv, kKwPregap))) {
                pregap += *frames;
                m.toc.at(last.next_index()).pregap_declared = true;
            }
            continue;
        }

        if (sv.starts_with("INDEX")) {
            std::string_view rest = past(sv, kKwIndex);
            const auto index = take_uint(rest, kCueFieldDigits);
            if (!index)
                continue;
            const auto frames = take_msf(rest);
            if (!frames)
                continue;

            if (*index == 0) {

                if (!pending.open)
                    pregap = *frames;
                continue;
            }
            if (*index != 1)
                continue;

            Track& t = m.toc.at(last.next_index());
            t.number = last.next_index().number();

            if (!pending.open) {

                if (m.table.count() == 0) {
                    return std::unexpected(last.refusal(Errc::bad_format, ERR_SITE()));
                }
                t.backing = m.toc.at(TrackIndex{}).backing;
                std::uint32_t start = *frames;
                if (t.pregap_declared)
                    start += pregap;

                std::int64_t file_base = std::int64_t{start} * std::int64_t{t.sector_size};
                if (!last.empty()) {

                    m.toc.at(last.last_index()).end = Lba{start};
                    if (pregap != 0) {
                        t.pregap = BlockCount{start - pregap};
                        if (!t.pregap_declared) {
                            file_base -= std::int64_t{kCdDataSize} * std::int64_t{t.pregap.v};
                        } else {
                            t.pregap = BlockCount{pregap};
                        }
                    }
                } else if (t.type != TrackType::Cdda) {
                    t.pregap = kRedBookPregap;
                }
                t.start = Lba{start};
                t.offset = std::int64_t{start} * std::int64_t{t.sector_size} - file_base;
            } else {

                if (m.table.count() >= kMaxTracks) {
                    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), m.table.count()});
                }
                t.backing = m.table.push(pending.take());

                t.pregap = BlockCount{*frames};
                if (t.type != TrackType::Cdda && last.empty()) {
                    t.pregap = kRedBookPregap;
                }
                t.start = m.toc.end;
                m.toc.end += pending.size.whole_units(proto::FileSize{t.sector_size});
                t.offset = std::int64_t{t.start.v} * std::int64_t{t.sector_size};
            }

            t.end = m.toc.end;
            last += TrackCount{1};
            if (last >= kMaxCueTracks)
                break;
            continue;
        }
    }

    if (last.empty()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    m.toc.sector_size = m.toc.at(TrackIndex{}).sector_size;

    close_toc(m.toc, last);
    return {};
}

Ex<void> IndexPairCueProgram::parse(std::string_view cue_path, const CueMount& m) const {
    m.cdda_order = CddaOrder::LittleEndian;

    auto text_r = m.io.read_cue_text(cue_path);
    if (!text_r)
        return std::unexpected(text_r.error());
    const std::string text = std::move(*text_r);
    const std::string_view base_dir = dir_of(cue_path);

    constexpr std::int64_t kLeadIn = std::int64_t{kRedBookPregap.v};
    constexpr std::int64_t kSs = std::int64_t{kCdDataSize};
    TrackCount last{};
    std::int64_t index0 = 0;
    std::int64_t index1 = 0;
    Lba disc_end{};
    std::string fname;
    PendingFile pending;

    LineReader reader(text);
    char line[1024];
    while (reader.next(std::span(line))) {
        std::string_view sv = lstrip(std::string_view(line));

        if (sv.starts_with("FILE")) {
            const CueFileDecl decl = take_cue_file(lstrip(past(sv, kKwFile)));
            const std::string_view rest = decl.format;
            fname.assign(base_dir);
            fname.append(decl.name);
            auto f = m.io.open_file(fname);
            if (!f)
                return std::unexpected(f.error());
            auto sz = (*f)->size();
            if (!sz)
                return std::unexpected(sz.error());
            if (rest.find("BINARY") == std::string_view::npos) {
                return std::unexpected(last.refusal(Errc::bad_format, ERR_SITE()));
            }
            pending.adopt(std::move(*f), *sz);
            continue;
        }

        if (sv.starts_with("TRACK")) {
            std::string_view rest = past(sv, kKwTrack);
            const auto number = take_uint(rest, kCueFieldDigits);
            if (!number)
                continue;
            index0 = 0;
            if (!last.next_index().number().matches(*number)) {
                return std::unexpected(last.refusal(Errc::bad_format, ERR_SITE()));
            }
            Track& t = m.toc.at(last.next_index());
            t.sector_size = kCdDataSize;
            if (last.empty())
                disc_end = Lba{} + kRedBookPregap;
            if (sv.find(kMode1Raw) != std::string_view::npos) {
                t.type = TrackType::Mode1;
            } else if (
                sv.find(kMode2Raw) != std::string_view::npos
                || sv.find("CDI/2352") != std::string_view::npos
            ) {
                t.type = TrackType::Mode2;
            } else if (sv.find("AUDIO") != std::string_view::npos) {
                t.type = TrackType::Cdda;
            } else {
                return std::unexpected(last.refusal(Errc::bad_format, ERR_SITE()));
            }
            continue;
        }

        if (sv.starts_with("INDEX")) {
            std::string_view rest = past(sv, kKwIndex);
            const auto index = take_uint(rest, kCueFieldDigits);
            if (!index)
                continue;
            const auto frames = take_msf(rest);
            if (!frames)
                continue;
            if (*index == 0) {
                index0 = *frames;
                continue;
            }
            if (*index != 1)
                continue;
            index1 = *frames;

            Track& t = m.toc.at(last.next_index());
            t.number = last.next_index().number();
            std::int64_t start = 0;
            std::int64_t stock_offset = 0;
            if (!pending.open) {

                if (last.empty())
                    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
                if (index0 == 0)
                    index0 = index1;
                start = index1 + kLeadIn;
                stock_offset = index0 * kSs;
                const std::int64_t pregap = index1 - index0;

                m.toc.at(last.last_index()).end = Lba{static_cast<std::uint32_t>(start - pregap)};

                t.backing = stock_offset != 0 ? m.toc.at(TrackIndex{}).backing : kNoBacking;
            } else {

                if (m.table.count() >= kMaxTracks) {
                    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), m.table.count()});
                }
                t.backing = m.table.push(pending.take());

                start = std::int64_t{disc_end.v} + index0 + index1;
                disc_end += pending.size.whole_units(proto::FileSize{kCdDataSize});
            }
            const std::int64_t pregap = index1 - index0;
            t.start = Lba{static_cast<std::uint32_t>(start)};
            t.pregap = BlockCount{static_cast<std::uint32_t>(pregap)};

            t.offset = (start - pregap) * kSs - stock_offset;
            t.end = disc_end;
            last += TrackCount{1};
            if (last >= kMaxCueTracks)
                break;
            continue;
        }
    }

    if (last.empty())
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});

    m.toc.end = disc_end;
    m.toc.sector_size = kCdDataSize;

    close_toc(m.toc, last);
    return {};
}

}
