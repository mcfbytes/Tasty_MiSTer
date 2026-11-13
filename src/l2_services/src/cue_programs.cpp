// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstring>
#include <memory>
#include <string>

#include "disc_lex.h"
#include "infra/ascii.h"
#include "infra/path_parts.h"
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

constexpr proto::BlockSize kCookedSector{kUserDataBytes};
constexpr proto::BlockSize kRawSector{kRawFrameBytes};

constexpr auto position_bytes = [](Lba pos, proto::BlockSize unit) noexcept {
    return static_cast<std::int64_t>(unit.position_of(pos));
};

}

Ex<void> SentinelCueProgram::parse(std::string_view cue_path, const CueMount& m) const {

    m.cdda_order = CddaOrder::LittleEndian;

    auto text_r = m.io.read_cue_text(cue_path);
    if (!text_r)
        return std::unexpected(text_r.error());
    const std::string text = std::move(*text_r);

    const std::string_view base_dir = infra::dir_prefix_any_sep(cue_path);

    TrackCount last{};
    BlockCount pregap{};

    BlockCount track_pregap{};
    FileSize hdr{};
    std::string fname;
    PendingFile pending;
    OpenFileId last_backing{};
    bool track0_size_stated = false;
    bool aborted = false;

    proto::BlockSize running_ss{};
    const bool pregap_relative = policy_.program == CuePolicy::Program::SentinelPregapRelative;

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

            constexpr FileSize kWavHeaderBytes{44};
            const bool wav_header = policy_.wav_header_skip && infra::iends_with(fname, ".wav");
            hdr = wav_header ? kWavHeaderBytes : FileSize{};
            pregap = BlockCount{};

            track_pregap = BlockCount{};
            m.toc.at(last.next_index()).offset = 0;
            continue;
        }

        if (sv.starts_with("TRACK")) {
            std::string_view rest = past(sv, kKwTrack);
            const auto number = take_uint(rest, kCueFieldDigits);
            if (!number)
                continue;

            if (!last.next_index().number().matches(CueField{*number})) {
                aborted = true;
                break;
            }

            track_pregap = BlockCount{};
            Track& t = m.toc.at(last.next_index());

            switch (policy_.type_detect) {
                case CuePolicy::TypeDetect::Track0Only:

                    if (last.empty()) {
                        if (sv.find(kMode1Cooked) != std::string_view::npos) {
                            t.sector_size = kUserDataBytes;
                            t.type = TrackType::Mode1;
                            track0_size_stated = true;
                        } else if (sv.find(kMode1Raw) != std::string_view::npos) {
                            t.sector_size = kRawFrameBytes;
                            t.type = TrackType::Mode1;
                            track0_size_stated = true;
                        }
                    }
                    break;
                case CuePolicy::TypeDetect::PerTrackRunningSize:

                    if (sv.find(kMode1Cooked) != std::string_view::npos) {
                        running_ss = kCookedSector;
                        t.type = TrackType::Mode1;
                    } else if (sv.find(kMode1Raw) != std::string_view::npos) {
                        running_ss = kRawSector;
                        t.type = TrackType::Mode1;
                    } else if (sv.find(kMode2Raw) != std::string_view::npos) {
                        running_ss = kRawSector;
                        t.type = TrackType::Mode2;
                    }
                    if (t.type != TrackType::Cdda)
                        t.sector_size = running_ss.offset_of(BlockCount{1});
                    break;
                case CuePolicy::TypeDetect::PerTrack:

                    if (sv.find(kMode1Cooked) != std::string_view::npos) {
                        t.sector_size = kUserDataBytes;
                        t.type = TrackType::Mode1;
                        if (last.empty())
                            track0_size_stated = true;
                    } else if (sv.find(kMode1Raw) != std::string_view::npos) {
                        t.sector_size = kRawFrameBytes;
                        t.type = TrackType::Mode1;
                        if (last.empty())
                            track0_size_stated = true;
                    } else if (sv.find("AUDIO") != std::string_view::npos) {
                        t.sector_size = kRawFrameBytes;
                        t.type = TrackType::Cdda;
                    }
                    break;
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
                    m.toc.at(last.last_index()).end = pregap_relative ? Lba{0} + pregap
                                                                      : Lba{0} + (*frames + pregap);
                }
                continue;
            }
            if (*index != 1)
                continue;

            Track& t = m.toc.at(last.next_index());
            t.number = last.next_index().number();
            const std::int64_t header_bytes = static_cast<std::int64_t>(hdr.v);

            const bool accumulate = (policy_.offset_op == CuePolicy::OffsetOp::Accumulate);
            if (accumulate) {
                t.offset += position_bytes(Lba{0} + pregap, kRawSector);
            }

            if (!pending.open) {

                if (m.table.count().empty()) {
                    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
                }
                t.backing = last_backing;
                t.start = Lba{0} + (*frames + pregap);
                if (!accumulate) {
                    t.offset = position_bytes(Lba{0} + pregap, proto::BlockSize{t.sector_size})
                        - header_bytes;
                }
                if (!last.empty() && m.toc.at(last.last_index()).end == Lba{0}) {
                    m.toc.at(last.last_index()).end = pregap_relative ? t.start - track_pregap
                                                                      : t.start;
                }
            } else {

                const auto pushed = m.table.push(pending.take());
                if (!pushed)
                    return std::unexpected(pushed.error());
                last_backing = *pushed;
                t.backing = last_backing;

                t.start = m.toc.end + pregap;

                proto::BlockSize ss{t.sector_size};
                switch (policy_.type_detect) {
                    case CuePolicy::TypeDetect::Track0Only:

                        ss = (t.type != TrackType::Cdda)
                            ? proto::BlockSize{m.toc.at(TrackIndex{}).sector_size}
                            : kRawSector;
                        break;
                    case CuePolicy::TypeDetect::PerTrack:
                    case CuePolicy::TypeDetect::PerTrackRunningSize:

                        break;
                }
                if (ss.empty())
                    ss = kRawSector;

                if (accumulate) {
                    t.offset += position_bytes(m.toc.end, kRawSector);
                } else {
                    t.offset = position_bytes(t.start, ss) - header_bytes;
                }

                const FileSize payload = pending.size.after_header(hdr);
                t.end = t.start + payload.units_covering(ss);
                t.start += *frames;
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
        m.toc.end += pregap;
        m.toc.at(last.last_index()).end = m.toc.end;
    }
    if (last.empty()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    switch (policy_.type_detect) {
        case CuePolicy::TypeDetect::PerTrackRunningSize:

            m.toc.sector_size = (running_ss.empty() ? kRawSector : running_ss)
                                    .offset_of(BlockCount{1});
            break;
        case CuePolicy::TypeDetect::Track0Only:
        case CuePolicy::TypeDetect::PerTrack:

            if (track0_size_stated) {
                m.toc.sector_size = m.toc.at(TrackIndex{}).sector_size;
            } else if (
                policy_.sniff_sega_disc_system && m.table.holds(m.toc.at(TrackIndex{}).backing)
            ) {
                std::byte head[16]{};
                IFile* f = m.table.file(m.toc.at(TrackIndex{}).backing);
                m.toc.sector_size = kRawFrameBytes;
                if (f != nullptr) {
                    auto rd = f->read_at(0, std::span<std::byte>(head, sizeof(head)));
                    if (rd
                        && *rd == sizeof(head)
                        && std::memcmp(head, kSegaMagic.data(), kSegaMagic.size()) == 0) {
                        m.toc.sector_size = kUserDataBytes;
                    }
                }
            } else {
                m.toc.sector_size = kRawFrameBytes;
            }
            break;
    }

    switch (policy_.type_detect) {
        case CuePolicy::TypeDetect::Track0Only:
        case CuePolicy::TypeDetect::PerTrackRunningSize:
            last.for_each_index([&](TrackIndex i) {
                Track& row = m.toc.at(i);
                if (row.type != TrackType::Cdda)
                    row.sector_size = m.toc.sector_size;
            });
            break;
        case CuePolicy::TypeDetect::PerTrack:

            break;
    }

    m.toc.close(last);

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

    const std::string_view base_dir = infra::dir_prefix_any_sep(cue_path);

    TrackCount last{};

    BlockCount pregap{};
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
            pregap = BlockCount{};

            if (!last.next_index().number().matches(CueField{*number})) {
                return std::unexpected(last.refusal(Errc::bad_format, ERR_SITE()));
            }
            Track& t = m.toc.at(last.next_index());
            if (sv.find(kMode1Raw) != std::string_view::npos
                || sv.find(kMode2Raw) != std::string_view::npos) {
                t.sector_size = kRawFrameBytes;
                t.type = TrackType::Mode1;

                if (last.empty())
                    m.toc.end = Lba{0} + kRedBookPregap;
            } else if (sv.find("AUDIO") != std::string_view::npos) {
                t.sector_size = kRawFrameBytes;
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

                if (m.table.count().empty()) {
                    return std::unexpected(last.refusal(Errc::bad_format, ERR_SITE()));
                }
                t.backing = m.toc.at(TrackIndex{}).backing;
                Lba start = Lba{0} + *frames;
                if (t.pregap_declared)
                    start += pregap;

                const std::int64_t start_bytes = position_bytes(
                    start,
                    proto::BlockSize{t.sector_size}
                );
                std::int64_t file_base = start_bytes;
                if (!last.empty()) {

                    m.toc.at(last.last_index()).end = start;
                    if (pregap != BlockCount{}) {
                        t.pregap = start - pregap - Lba{0};
                        if (!t.pregap_declared) {
                            file_base -= position_bytes(Lba{0} + t.pregap, kRawSector);
                        } else {
                            t.pregap = pregap;
                        }
                    }
                } else if (t.type != TrackType::Cdda) {
                    t.pregap = kRedBookPregap;
                }
                t.start = start;
                t.offset = start_bytes - file_base;
            } else {

                const auto pushed = m.table.push(pending.take());
                if (!pushed)
                    return std::unexpected(pushed.error());
                t.backing = *pushed;

                t.pregap = *frames;
                if (t.type != TrackType::Cdda && last.empty()) {
                    t.pregap = kRedBookPregap;
                }
                t.start = m.toc.end;
                const proto::BlockSize track_ss{t.sector_size};
                m.toc.end += pending.size.whole_units(track_ss);
                t.offset = position_bytes(t.start, track_ss);
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

    m.toc.close(last);
    return {};
}

void IndexTripleCueProgram::shift_chd_toc_(Toc& toc, TrackCount count) const {
    count.for_each_index([&](TrackIndex i) {
        Track& t = toc.at(i);
        if (i == TrackIndex{}) {

            t.pregap = kRedBookPregap;
            t.start = Lba{0} + kRedBookPregap;
            t.end += kRedBookPregap;
        } else {

            const BlockCount frame_cnt = (t.end - t.start) + t.pregap;
            t.start = toc.at(i - TrackCount{1}).end;
            t.end = t.start + frame_cnt;
        }
        t.offset -= std::int64_t{kRedBookPregap.v};
    });

    toc.end = toc.at(count.last_index()).end;
}

Ex<void> IndexPairCueProgram::parse(std::string_view cue_path, const CueMount& m) const {
    m.cdda_order = CddaOrder::LittleEndian;

    auto text_r = m.io.read_cue_text(cue_path);
    if (!text_r)
        return std::unexpected(text_r.error());
    const std::string text = std::move(*text_r);
    const std::string_view base_dir = infra::dir_prefix_any_sep(cue_path);

    TrackCount last{};
    BlockCount index0{};
    BlockCount index1{};
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
            index0 = BlockCount{};
            if (!last.next_index().number().matches(CueField{*number})) {
                return std::unexpected(last.refusal(Errc::bad_format, ERR_SITE()));
            }
            Track& t = m.toc.at(last.next_index());
            t.sector_size = kRawFrameBytes;
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
            const bool shared_file = !pending.open;
            if (shared_file) {

                if (last.empty())
                    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
                if (index0 == BlockCount{})
                    index0 = index1;
            }

            const BlockCount pregap = index1 - index0;
            Lba start{};
            std::int64_t file_offset = 0;
            if (shared_file) {
                start = Lba{0} + kRedBookPregap + index1;

                t.backing = (index0 != BlockCount{}) ? m.toc.at(TrackIndex{}).backing : kNoBacking;
                file_offset = position_bytes(Lba{0} + index0, kRawSector);

                m.toc.at(last.last_index()).end = start - pregap;
            } else {

                const auto pushed = m.table.push(pending.take());
                if (!pushed)
                    return std::unexpected(pushed.error());
                t.backing = *pushed;
                start = disc_end + index0 + index1;
                disc_end += pending.size.whole_units(kRawSector);
            }
            t.start = start;
            t.pregap = pregap;

            t.offset = position_bytes(start - pregap, kRawSector) - file_offset;
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
    m.toc.sector_size = kRawFrameBytes;

    m.toc.close(last);
    return {};
}

void IndexPairCueProgram::shift_chd_toc_(Toc& toc, TrackCount count) const {
    count.for_each_index([&](TrackIndex i) {
        Track& t = toc.at(i);
        t.start += kRedBookPregap;
        t.end += kRedBookPregap;
        t.offset -= std::int64_t{kRedBookPregap.v};
    });
    toc.end += kRedBookPregap;
}

bool IndexPairCueProgram::pregap_in_own_file_() const {
    return true;
}

}
