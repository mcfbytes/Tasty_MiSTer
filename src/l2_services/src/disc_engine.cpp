// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/disc_engine.h"

#include "svc/chd_prefetch.h"
#include "svc/rw_interleaver.h"
#include "svc/sector_descrambler.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <string>

#include "vfs_detail.h"

#if defined(TASTY_HAVE_LIBCHDR)
#include <libchdr/chd.h>
#endif

namespace mister::svc {

namespace {

constexpr std::uint32_t kFramesPerSecond = 75;
constexpr std::size_t kSubcodeBytes = 96;
constexpr std::size_t kUserDataBytes = 2048;
constexpr std::uint32_t kMode2Cooked = 2336;
constexpr std::uint32_t kMaxCueTracks = 99;

char lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool ends_with_ci(std::string_view s, std::string_view suffix) noexcept {
    if (s.size() < suffix.size()) return false;
    const std::size_t off = s.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        if (lower(s[off + i]) != lower(suffix[i])) return false;
    }
    return true;
}

std::string_view dir_of(std::string_view path) noexcept {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? std::string_view{} : path.substr(0, slash + 1);
}

std::string_view lstrip(std::string_view s) noexcept {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
        ++i;
    return s.substr(i);
}

constexpr std::size_t kNoDigitLimit = 0;
constexpr std::size_t kCueFieldDigits = 2;

bool take_uint(std::string_view& s, std::uint32_t& out,
               std::size_t max_digits = kNoDigitLimit) noexcept {
    s = lstrip(s);
    const std::size_t span =
        (max_digits == kNoDigitLimit) ? s.size() : std::min(s.size(), max_digits);
    const char* first = s.data();
    const char* last = s.data() + span;
    const auto r = std::from_chars(first, last, out);
    if (r.ec != std::errc{} || r.ptr == first) return false;
    s.remove_prefix(static_cast<std::size_t>(r.ptr - first));
    return true;
}

bool take_msf(std::string_view s, std::uint32_t& frames) noexcept {
    std::uint32_t m = 0, sec = 0, f = 0;
    if (!take_uint(s, m, kCueFieldDigits)) return false;
    if (s.empty() || s.front() != ':') return false;
    s.remove_prefix(1);
    if (!take_uint(s, sec, kCueFieldDigits)) return false;
    if (s.empty() || s.front() != ':') return false;
    s.remove_prefix(1);
    if (!take_uint(s, f, kCueFieldDigits)) return false;
    frames = m * 60 * kFramesPerSecond + sec * kFramesPerSecond + f;
    return true;
}

std::uint8_t bcd(std::uint32_t v) noexcept {
    return static_cast<std::uint8_t>((((v / 10) % 10) << 4) | (v % 10));
}

class LineReader {
public:
    explicit LineReader(std::string_view text) noexcept : text_(text) {}

    bool next(char* out, std::size_t cap) noexcept {
        do {
            std::size_t n = 0;
            out[0] = '\0';
            while (pos_ < text_.size() && text_[pos_] != '\n') {
                const char c = text_[pos_++];
                if (c == '\r') continue;
                if (n + 1 < cap) {
                    out[n++] = c;
                    out[n] = '\0';
                }
            }
            if (pos_ < text_.size()) ++pos_;
        } while (out[0] == '\0' && pos_ < text_.size());
        return out[0] != '\0';
    }

private:
    std::string_view text_;
    std::size_t pos_ = 0;
};

struct ChdTypeRow {
    std::string_view name;
    std::uint32_t sector_size;
    TrackType type;
};
constexpr ChdTypeRow kChdTypes[] = {
    {"MODE1_RAW", 2352, TrackType::Mode1}, {"MODE2_RAW", 2352, TrackType::Mode2},
    {"MODE1", 2048, TrackType::Mode1},     {"MODE2", kMode2Cooked, TrackType::Mode2},
    {"AUDIO", 2352, TrackType::Cdda},
};

struct ChdTrackMeta {
    std::uint32_t frames = 0;
    std::uint32_t pregap = 0;
    std::uint32_t postgap = 0;
    char type[32]{};
    char subtype[32]{};
    char pgtype[32]{};
};

bool take_key(std::string_view& s, std::string_view key) noexcept {
    s = lstrip(s);
    if (!s.starts_with(key)) return false;
    s.remove_prefix(key.size());
    return true;
}

bool take_token(std::string_view& s, char* out, std::size_t cap) noexcept {
    s = lstrip(s);
    std::size_t n = 0;
    while (n + 1 < cap && n < s.size() && s[n] != ' ' && s[n] != '\t') {
        out[n] = s[n];
        ++n;
    }
    out[n] = '\0';
    s.remove_prefix(n);
    return n != 0;
}

bool parse_chd_meta(const ChdMetaRow& row, ChdTrackMeta& out) noexcept {
    std::string_view s = row.text;
    std::uint32_t id = 0;
    char pgsub[32]{};
    if (!take_key(s, "TRACK:") || !take_uint(s, id)) return false;
    if (!take_key(s, "TYPE:") || !take_token(s, out.type, sizeof(out.type))) return false;
    if (!take_key(s, "SUBTYPE:") || !take_token(s, out.subtype, sizeof(out.subtype))) return false;
    if (!take_key(s, "FRAMES:") || !take_uint(s, out.frames)) return false;
    if (!row.v2) {

        out.pregap = 0;
        out.postgap = 0;
        out.pgtype[0] = '\0';
        return true;
    }
    if (!take_key(s, "PREGAP:") || !take_uint(s, out.pregap)) return false;
    if (!take_key(s, "PGTYPE:") || !take_token(s, out.pgtype, sizeof(out.pgtype))) return false;
    if (!take_key(s, "PGSUB:") || !take_token(s, pgsub, sizeof(pgsub))) return false;
    if (!take_key(s, "POSTGAP:") || !take_uint(s, out.postgap)) return false;
    return true;
}

std::uint64_t cue_file_pos(const Track& t) noexcept {
    const std::int64_t pos =
        static_cast<std::int64_t>(t.start.v) * static_cast<std::int64_t>(t.sector_size) - t.offset;
    return pos > 0 ? static_cast<std::uint64_t>(pos) : 0u;
}

}  // namespace

static Ex<void> check_policy(const CuePolicy& policy, std::uint16_t site) {
    const CueAxis axis = unimplemented_axis(policy);
    if (axis != CueAxis::None) {
        return std::unexpected(Error{Errc::unimplemented, site, static_cast<std::uint32_t>(axis)});
    }
    return {};
}

DiscEngine::DiscEngine(CuePolicy p) : policy_(p) {
    switch (p.frame_shaping) {
        case CuePolicy::FrameShaping::None:
            break;
        case CuePolicy::FrameShaping::DescrambleByHeader:
            frame_shaper_ = std::make_unique<SectorDescrambler>();
            break;
    }
}

Ex<DiscEngine> DiscEngine::create(const Vfs& vfs, CuePolicy policy) {
    auto ok = check_policy(policy, ERR_SITE());
    if (!ok) return std::unexpected(ok.error());
    DiscEngine d(policy);
    d.vfs_ = &vfs;
    return d;
}

Ex<DiscEngine> DiscEngine::create(const IImageOpener& opener, CuePolicy policy) {
    auto ok = check_policy(policy, ERR_SITE());
    if (!ok) return std::unexpected(ok.error());
    DiscEngine d(policy);
    d.opener_ = &opener;
    return d;
}

Ex<std::unique_ptr<IFile>> DiscEngine::open_file(std::string_view path) const {
    Ex<std::unique_ptr<IFile>> f = std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    if (opener_ != nullptr) {
        f = opener_->open_read(path);
    } else if (vfs_ != nullptr) {
        f = vfs_->open(path, OpenMode::Read);
    }
    if (f) (void)(*f)->advise(IFile::Access::Sequential, 0, 0);
    return f;
}

Ex<void> DiscEngine::mount(std::string_view image_path) {
    if (image_path.empty()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    if (detail::zip_split(image_path).zipped) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    (void)unmount();

    Ex<void> r = {};

    bool image_named_sub = false;
    if (ends_with_ci(image_path, ".cue")) {
        r = mount_cue(image_path);
        image_named_sub = true;
    } else if (ends_with_ci(image_path, ".chd")) {
        image_named_sub = true;
#if defined(TASTY_HAVE_LIBCHDR)
        r = mount_chd_path(image_path);
#else

        r = std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});
#endif
    } else {
        r = mount_iso(image_path);
    }

    if (!r) {
        (void)unmount();
        return r;
    }

    if (chd_ == nullptr && toc_.last != 0) {
        const Track& t0 = toc_.tracks[0];
        if (t0.backing < file_count_ && files_[t0.backing]) {
            constexpr std::uint64_t kFirstWindowBytes = 1u * 1024u * 1024u;
            (void)files_[t0.backing]->advise(IFile::Access::WillNeed, cue_file_pos(t0),
                                             kFirstWindowBytes);
        }
    }

    if (policy_.sub_naming == CuePolicy::SubNaming::ImageSubOrCdg && chd_ == nullptr &&
        ends_with_ci(image_path, ".cue")) {
        open_sub_or_cdg_(image_path);
    }
    if (image_named_sub && policy_.sub_naming == CuePolicy::SubNaming::ImageFilename &&
        image_path.size() > 4) {
        std::string sub_name(image_path.substr(0, image_path.size() - 4));
        sub_name.append(".sub");
        auto s = open_file(sub_name);
        if (s) {
            sub_ = std::move(*s);
            toc_.has_subcode = true;
        }
    }
    mounted_ = true;
    return {};
}

Ex<void> DiscEngine::unmount() {

    if (prefetch_ != nullptr) {
        if (!prefetch_->settle_park()) ++park_timeouts_;
    }
    for (auto& f : files_)
        f.reset();
    file_count_ = 0;
    sub_.reset();
    sub_origin_ = 0;
    sub_cdg_ = false;
    sub_shaper_.reset();
    chd_.reset();
    hunk_buf_.reset();
    hunk_bytes_ = 0;
    sectors_per_hunk_ = 0;
    hunk_count_ = 0;
    hunk_memo_ = kNoHunk;
    hunk_ptr_ = nullptr;
    prefetch_depth_ = 0;
    sync_decompress_ = 0;
    toc_ = Toc{};
    mounted_ = false;
    return {};
}

Ex<std::string> DiscEngine::read_cue_text(std::string_view cue_path) const {
    auto cue = open_file(cue_path);
    if (!cue) return std::unexpected(cue.error());
    auto cue_size = (*cue)->size();
    if (!cue_size) return std::unexpected(cue_size.error());

    constexpr std::uint64_t kMaxCueBytes = 100u * 1024u;
    const std::size_t want =
        static_cast<std::size_t>(std::min<std::uint64_t>(cue_size->v, kMaxCueBytes));
    std::string text(want, '\0');
    if (want != 0) {
        auto rd = (*cue)->read_at(0, std::as_writable_bytes(std::span<char>(text.data(), want)));
        if (!rd) return std::unexpected(rd.error());
        text.resize(*rd);
    }
    return text;
}

Ex<void> DiscEngine::mount_cue(std::string_view cue_path) {
    if (policy_.pregap_model == CuePolicy::PregapModel::IndexTriple) {
        return mount_cue_index_triple(cue_path);
    }
    if (policy_.pregap_model == CuePolicy::PregapModel::IndexPair) {
        return mount_cue_index_pair(cue_path);
    }

    cdda_order_ = CddaOrder::LittleEndian;

    auto text_r = read_cue_text(cue_path);
    if (!text_r) return std::unexpected(text_r.error());
    const std::string text = std::move(*text_r);

    const std::string_view base_dir = dir_of(cue_path);

    std::uint32_t last = 0;
    std::uint32_t pregap = 0;

    std::uint32_t track_pregap = 0;
    std::uint32_t hdr = 0;
    std::string fname;
    bool pending_open = false;
    std::uint64_t pending_size = 0;
    std::unique_ptr<IFile> pending_file;
    std::uint16_t last_backing = 0;
    bool track0_size_stated = false;
    bool aborted = false;

    std::uint32_t running_ss = 0;
    const bool pregap_relative =
        policy_.track_close == CuePolicy::TrackClose::SentinelPregapRelative;

    LineReader reader(text);
    char line[1024];
    while (reader.next(line, sizeof(line))) {
        std::string_view sv = lstrip(std::string_view(line));

        if (sv.starts_with("FILE")) {
            std::string_view rest = lstrip(sv.substr(4));
            std::string_view name;
            if (!rest.empty() && rest.front() == '"') {
                rest.remove_prefix(1);
                const std::size_t q = rest.find('"');
                name = rest.substr(0, q == std::string_view::npos ? rest.size() : q);
                rest = (q == std::string_view::npos) ? std::string_view{} : rest.substr(q);
            } else {
                const std::size_t sp = rest.find(' ');
                name = rest.substr(0, sp == std::string_view::npos ? rest.size() : sp);
                rest = (sp == std::string_view::npos) ? std::string_view{} : rest.substr(sp);
            }

            fname.assign(base_dir);
            fname.append(name);

            auto f = open_file(fname);
            if (!f) return std::unexpected(f.error());
            auto sz = (*f)->size();
            if (!sz) return std::unexpected(sz.error());

            if (rest.find("BINARY") == std::string_view::npos &&
                rest.find("MOTOROLA") == std::string_view::npos &&
                rest.find("WAVE") == std::string_view::npos) {
                return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
            }

            pending_file = std::move(*f);
            pending_size = sz->v;
            pending_open = true;

            hdr = (policy_.wav_header_skip && ends_with_ci(fname, ".wav")) ? 44u : 0u;
            pregap = 0;

            track_pregap = 0;
            toc_.tracks[last].offset = 0;
            continue;
        }

        if (sv.starts_with("TRACK")) {
            std::string_view rest = sv.substr(5);
            std::uint32_t number = 0;
            if (!take_uint(rest, number, kCueFieldDigits)) continue;

            if (number != last + 1) {
                aborted = true;
                break;
            }

            track_pregap = 0;
            Track& t = toc_.tracks[last];
            if (policy_.type_detect == CuePolicy::TypeDetect::Track0Only) {

                if (last == 0) {
                    if (sv.find("MODE1/2048") != std::string_view::npos) {
                        t.sector_size = 2048;
                        t.type = TrackType::Mode1;
                        track0_size_stated = true;
                    } else if (sv.find("MODE1/2352") != std::string_view::npos) {
                        t.sector_size = kCdDataSize;
                        t.type = TrackType::Mode1;
                        track0_size_stated = true;
                    }
                }
            } else if (policy_.type_detect == CuePolicy::TypeDetect::PerTrackRunningSize) {

                if (sv.find("MODE1/2048") != std::string_view::npos) {
                    running_ss = 2048;
                    t.type = TrackType::Mode1;
                } else if (sv.find("MODE1/2352") != std::string_view::npos) {
                    running_ss = kCdDataSize;
                    t.type = TrackType::Mode1;
                } else if (sv.find("MODE2/2352") != std::string_view::npos) {
                    running_ss = kCdDataSize;
                    t.type = TrackType::Mode2;
                }
                if (t.type != TrackType::Cdda) t.sector_size = running_ss;
            } else {

                if (sv.find("MODE1/2048") != std::string_view::npos) {
                    t.sector_size = 2048;
                    t.type = TrackType::Mode1;
                    if (last == 0) track0_size_stated = true;
                } else if (sv.find("MODE1/2352") != std::string_view::npos) {
                    t.sector_size = kCdDataSize;
                    t.type = TrackType::Mode1;
                    if (last == 0) track0_size_stated = true;
                } else if (sv.find("AUDIO") != std::string_view::npos) {
                    t.sector_size = kCdDataSize;
                    t.type = TrackType::Cdda;
                }
            }

            if (last != 0 && !pending_open) toc_.tracks[last - 1].end = Lba{0};
            continue;
        }

        if (sv.starts_with("PREGAP")) {
            std::uint32_t frames = 0;
            if (take_msf(sv.substr(6), frames)) {
                pregap += frames;
                track_pregap = frames;
            }
            continue;
        }

        if (sv.starts_with("INDEX")) {
            std::string_view rest = sv.substr(5);
            std::uint32_t index = 0;
            std::uint32_t frames = 0;
            if (!take_uint(rest, index, kCueFieldDigits)) continue;
            if (!take_msf(rest, frames)) continue;

            if (index == 0) {

                if (last != 0 && toc_.tracks[last - 1].end == Lba{0}) {
                    toc_.tracks[last - 1].end =
                        pregap_relative ? Lba{pregap} : Lba{frames + pregap};
                }
                continue;
            }
            if (index != 1) continue;

            Track& t = toc_.tracks[last];
            t.number = TrackNumber{static_cast<std::uint8_t>(last + 1)};

            const bool accumulate = (policy_.offset_op == CuePolicy::OffsetOp::Accumulate);
            if (accumulate) {
                t.offset +=
                    static_cast<std::int64_t>(pregap) * static_cast<std::int64_t>(kCdDataSize);
            }

            if (!pending_open) {

                if (file_count_ == 0) {
                    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
                }
                t.backing = last_backing;
                t.start = Lba{frames + pregap};
                if (!accumulate) {
                    t.offset = static_cast<std::int64_t>(pregap) *
                                   static_cast<std::int64_t>(t.sector_size) -
                               static_cast<std::int64_t>(hdr);
                }
                if (last != 0 && toc_.tracks[last - 1].end == Lba{0}) {
                    toc_.tracks[last - 1].end =
                        pregap_relative ? Lba{t.start.v - track_pregap} : t.start;
                }
            } else {

                if (file_count_ >= kMaxTracks) {
                    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), file_count_});
                }
                files_[file_count_] = std::move(pending_file);
                last_backing = file_count_;
                ++file_count_;
                pending_open = false;
                t.backing = last_backing;

                t.start = Lba{toc_.end.v + pregap};

                std::uint32_t ss = t.sector_size;
                if (policy_.type_detect == CuePolicy::TypeDetect::Track0Only) {
                    ss = (t.type != TrackType::Cdda) ? toc_.tracks[0].sector_size
                                                     : static_cast<std::uint32_t>(kCdDataSize);
                }
                if (ss == 0) ss = static_cast<std::uint32_t>(kCdDataSize);

                if (accumulate) {
                    t.offset += static_cast<std::int64_t>(toc_.end.v) *
                                static_cast<std::int64_t>(kCdDataSize);
                } else {
                    t.offset =
                        static_cast<std::int64_t>(t.start.v) * static_cast<std::int64_t>(ss) -
                        static_cast<std::int64_t>(hdr);
                }

                const std::uint64_t payload = (pending_size > hdr) ? pending_size - hdr : 0;
                const std::uint64_t sectors = (payload + ss - 1) / ss;
                t.end = Lba{t.start.v + static_cast<std::uint32_t>(sectors)};
                t.start = Lba{t.start.v + frames};
                toc_.end = t.end;
            }

            ++last;
            if (last == kMaxCueTracks) break;
            continue;
        }
    }

    if (aborted) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), last});
    }

    if (last != 0 && toc_.tracks[last - 1].end == Lba{0}) {
        toc_.end = Lba{toc_.end.v + pregap};
        toc_.tracks[last - 1].end = toc_.end;
    }
    if (last == 0) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    toc_.last = static_cast<std::uint8_t>(last);
    toc_.offset_unit = OffsetUnit::CueBytes;

    if (policy_.type_detect == CuePolicy::TypeDetect::PerTrackRunningSize) {

        toc_.sector_size = (running_ss != 0) ? running_ss : kCdDataSize;
    } else if (track0_size_stated) {
        toc_.sector_size = toc_.tracks[0].sector_size;
    } else if (policy_.sniff_sega_disc_system && toc_.tracks[0].backing < file_count_) {
        std::byte head[16]{};
        IFile* f = files_[toc_.tracks[0].backing].get();
        toc_.sector_size = kCdDataSize;
        if (f != nullptr) {
            auto rd = f->read_at(0, std::span<std::byte>(head, sizeof(head)));
            if (rd && *rd == sizeof(head) && std::memcmp(head, "SEGADISCSYSTEM", 14) == 0) {
                toc_.sector_size = 2048;
            }
        }
    } else {
        toc_.sector_size = kCdDataSize;
    }

    if (policy_.type_detect != CuePolicy::TypeDetect::PerTrack) {
        for (std::uint32_t i = 0; i < last; ++i) {
            if (toc_.tracks[i].type != TrackType::Cdda) {
                toc_.tracks[i].sector_size = toc_.sector_size;
            }
        }
    }

    Track& lead_out = toc_.tracks[last];
    lead_out.number = TrackNumber{static_cast<std::uint8_t>(last + 1)};
    lead_out.type = TrackType::Cdda;
    lead_out.start = toc_.end;
    lead_out.end = toc_.end;

    std::string sub_name;
    if (policy_.sub_naming == CuePolicy::SubNaming::LastTrackFilename && fname.size() > 4) {
        sub_name.assign(fname, 0, fname.size() - 4);
    }
    if (!sub_name.empty()) {
        sub_name.append(".sub");
        auto s = open_file(sub_name);
        if (s) {
            sub_ = std::move(*s);
            toc_.has_subcode = true;
        }
    }
    return {};
}

Ex<void> DiscEngine::mount_cue_index_triple(std::string_view cue_path) {
    cdda_order_ = CddaOrder::LittleEndian;

    auto text_r = read_cue_text(cue_path);
    if (!text_r) return std::unexpected(text_r.error());
    const std::string text = std::move(*text_r);

    const std::string_view base_dir = dir_of(cue_path);

    std::uint32_t last = 0;

    std::uint32_t pregap = 0;
    std::string fname;
    bool pending_open = false;
    std::uint64_t pending_size = 0;
    std::unique_ptr<IFile> pending_file;

    LineReader reader(text);
    char line[1024];
    while (reader.next(line, sizeof(line))) {
        std::string_view sv = lstrip(std::string_view(line));

        if (sv.starts_with("FILE")) {
            std::string_view rest = lstrip(sv.substr(4));
            std::string_view name;
            if (!rest.empty() && rest.front() == '"') {
                rest.remove_prefix(1);
                const std::size_t q = rest.find('"');
                name = rest.substr(0, q == std::string_view::npos ? rest.size() : q);
                rest = (q == std::string_view::npos) ? std::string_view{} : rest.substr(q);
            } else {
                const std::size_t sp = rest.find(' ');
                name = rest.substr(0, sp == std::string_view::npos ? rest.size() : sp);
                rest = (sp == std::string_view::npos) ? std::string_view{} : rest.substr(sp);
            }
            fname.assign(base_dir);
            fname.append(name);

            auto f = open_file(fname);
            if (!f) return std::unexpected(f.error());
            auto sz = (*f)->size();
            if (!sz) return std::unexpected(sz.error());

            if (rest.find("BINARY") == std::string_view::npos) {
                return std::unexpected(Error{Errc::bad_format, ERR_SITE(), last});
            }
            pending_file = std::move(*f);
            pending_size = sz->v;
            pending_open = true;
            continue;
        }

        if (sv.starts_with("TRACK")) {
            std::string_view rest = sv.substr(5);
            std::uint32_t number = 0;
            if (!take_uint(rest, number, kCueFieldDigits)) continue;
            pregap = 0;

            if (number != last + 1) {
                return std::unexpected(Error{Errc::bad_format, ERR_SITE(), last});
            }
            Track& t = toc_.tracks[last];
            if (sv.find("MODE1/2352") != std::string_view::npos ||
                sv.find("MODE2/2352") != std::string_view::npos) {
                t.sector_size = kCdDataSize;
                t.type = TrackType::Mode1;

                if (last == 0) toc_.end = Lba{static_cast<std::uint32_t>(kRedBookPregap)};
            } else if (sv.find("AUDIO") != std::string_view::npos) {
                t.sector_size = kCdDataSize;
                t.type = TrackType::Cdda;
            } else {

                return std::unexpected(Error{Errc::bad_format, ERR_SITE(), last});
            }
            continue;
        }

        if (sv.starts_with("PREGAP")) {
            std::uint32_t frames = 0;
            if (take_msf(sv.substr(6), frames)) {
                pregap += frames;
                toc_.tracks[last].pregap_declared = true;
            }
            continue;
        }

        if (sv.starts_with("INDEX")) {
            std::string_view rest = sv.substr(5);
            std::uint32_t index = 0;
            std::uint32_t frames = 0;
            if (!take_uint(rest, index, kCueFieldDigits)) continue;
            if (!take_msf(rest, frames)) continue;

            if (index == 0) {

                if (!pending_open) pregap = frames;
                continue;
            }
            if (index != 1) continue;

            Track& t = toc_.tracks[last];
            t.number = TrackNumber{static_cast<std::uint8_t>(last + 1)};

            if (!pending_open) {

                if (file_count_ == 0) {
                    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), last});
                }
                t.backing = toc_.tracks[0].backing;
                std::uint32_t start = frames;
                if (t.pregap_declared) start += pregap;

                std::int64_t file_base =
                    static_cast<std::int64_t>(start) * static_cast<std::int64_t>(t.sector_size);
                if (last != 0) {

                    toc_.tracks[last - 1].end = Lba{start};
                    if (pregap != 0) {
                        t.pregap = start - pregap;
                        if (!t.pregap_declared) {
                            file_base -= static_cast<std::int64_t>(kCdDataSize) *
                                         static_cast<std::int64_t>(t.pregap);
                        } else {
                            t.pregap = pregap;
                        }
                    }
                } else if (t.type != TrackType::Cdda) {
                    t.pregap = static_cast<std::uint32_t>(kRedBookPregap);
                }
                t.start = Lba{start};
                t.offset =
                    static_cast<std::int64_t>(start) * static_cast<std::int64_t>(t.sector_size) -
                    file_base;
            } else {

                if (file_count_ >= kMaxTracks) {
                    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), file_count_});
                }
                files_[file_count_] = std::move(pending_file);
                t.backing = file_count_;
                ++file_count_;
                pending_open = false;

                t.pregap = frames;
                if (t.type != TrackType::Cdda && last == 0) {
                    t.pregap = static_cast<std::uint32_t>(kRedBookPregap);
                }
                t.start = toc_.end;
                toc_.end =
                    Lba{toc_.end.v + static_cast<std::uint32_t>(pending_size / t.sector_size)};
                t.offset =
                    static_cast<std::int64_t>(t.start.v) * static_cast<std::int64_t>(t.sector_size);
            }

            t.end = toc_.end;
            ++last;
            if (last >= kMaxCueTracks) break;
            continue;
        }
    }

    if (last == 0) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    toc_.last = static_cast<std::uint8_t>(last);
    toc_.sector_size = toc_.tracks[0].sector_size;
    toc_.offset_unit = OffsetUnit::CueBytes;

    Track& lead_out = toc_.tracks[last];
    lead_out.number = TrackNumber{static_cast<std::uint8_t>(last + 1)};
    lead_out.type = TrackType::Cdda;
    lead_out.start = toc_.end;
    lead_out.end = toc_.end;
    return {};
}

Ex<void> DiscEngine::mount_cue_index_pair(std::string_view cue_path) {
    cdda_order_ = CddaOrder::LittleEndian;

    auto text_r = read_cue_text(cue_path);
    if (!text_r) return std::unexpected(text_r.error());
    const std::string text = std::move(*text_r);
    const std::string_view base_dir = dir_of(cue_path);

    constexpr auto kLeadIn = static_cast<std::int64_t>(kRedBookPregap);
    constexpr auto kSs = static_cast<std::int64_t>(kCdDataSize);
    std::uint32_t last = 0;
    std::int64_t index0 = 0;
    std::int64_t index1 = 0;
    std::int64_t disc_end = 0;
    std::string fname;
    bool pending_open = false;
    std::uint64_t pending_size = 0;
    std::unique_ptr<IFile> pending_file;

    LineReader reader(text);
    char line[1024];
    while (reader.next(line, sizeof(line))) {
        std::string_view sv = lstrip(std::string_view(line));

        if (sv.starts_with("FILE")) {
            std::string_view rest = lstrip(sv.substr(4));
            std::string_view name;
            if (!rest.empty() && rest.front() == '"') {
                rest.remove_prefix(1);
                const std::size_t q = rest.find('"');
                name = rest.substr(0, q == std::string_view::npos ? rest.size() : q);
                rest = (q == std::string_view::npos) ? std::string_view{} : rest.substr(q);
            } else {
                const std::size_t sp = rest.find(' ');
                name = rest.substr(0, sp == std::string_view::npos ? rest.size() : sp);
                rest = (sp == std::string_view::npos) ? std::string_view{} : rest.substr(sp);
            }
            fname.assign(base_dir);
            fname.append(name);
            auto f = open_file(fname);
            if (!f) return std::unexpected(f.error());
            auto sz = (*f)->size();
            if (!sz) return std::unexpected(sz.error());
            if (rest.find("BINARY") == std::string_view::npos) {
                return std::unexpected(Error{Errc::bad_format, ERR_SITE(), last});
            }
            pending_file = std::move(*f);
            pending_size = sz->v;
            pending_open = true;
            continue;
        }

        if (sv.starts_with("TRACK")) {
            std::string_view rest = sv.substr(5);
            std::uint32_t number = 0;
            if (!take_uint(rest, number, kCueFieldDigits)) continue;
            index0 = 0;
            if (number != last + 1) {
                return std::unexpected(Error{Errc::bad_format, ERR_SITE(), last});
            }
            Track& t = toc_.tracks[last];
            t.sector_size = kCdDataSize;
            if (last == 0) disc_end = kLeadIn;
            if (sv.find("MODE1/2352") != std::string_view::npos) {
                t.type = TrackType::Mode1;
            } else if (sv.find("MODE2/2352") != std::string_view::npos ||
                       sv.find("CDI/2352") != std::string_view::npos) {
                t.type = TrackType::Mode2;
            } else if (sv.find("AUDIO") != std::string_view::npos) {
                t.type = TrackType::Cdda;
            } else {
                return std::unexpected(Error{Errc::bad_format, ERR_SITE(), last});
            }
            continue;
        }

        if (sv.starts_with("INDEX")) {
            std::string_view rest = sv.substr(5);
            std::uint32_t index = 0;
            std::uint32_t frames = 0;
            if (!take_uint(rest, index, kCueFieldDigits)) continue;
            if (!take_msf(rest, frames)) continue;
            if (index == 0) {
                index0 = frames;
                continue;
            }
            if (index != 1) continue;
            index1 = frames;

            Track& t = toc_.tracks[last];
            t.number = TrackNumber{static_cast<std::uint8_t>(last + 1)};
            std::int64_t start = 0;
            std::int64_t stock_offset = 0;
            if (!pending_open) {

                if (last == 0) return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
                if (index0 == 0) index0 = index1;
                start = index1 + kLeadIn;
                stock_offset = index0 * kSs;
                const std::int64_t pregap = index1 - index0;

                toc_.tracks[last - 1].end = Lba{static_cast<std::uint32_t>(start - pregap)};

                t.backing = stock_offset != 0 ? toc_.tracks[0].backing
                                              : static_cast<std::uint16_t>(kMaxTracks);
            } else {

                if (file_count_ >= kMaxTracks) {
                    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), file_count_});
                }
                files_[file_count_] = std::move(pending_file);
                t.backing = file_count_;
                ++file_count_;
                pending_open = false;
                start = disc_end + index0 + index1;
                disc_end += static_cast<std::int64_t>(pending_size / kCdDataSize);
            }
            const std::int64_t pregap = index1 - index0;
            t.start = Lba{static_cast<std::uint32_t>(start)};
            t.pregap = static_cast<std::uint32_t>(pregap);

            t.offset = (start - pregap) * kSs - stock_offset;
            t.end = Lba{static_cast<std::uint32_t>(disc_end)};
            ++last;
            if (last >= kMaxCueTracks) break;
            continue;
        }
    }

    if (last == 0) return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});

    toc_.last = static_cast<std::uint8_t>(last);
    toc_.end = Lba{static_cast<std::uint32_t>(disc_end)};
    toc_.sector_size = kCdDataSize;
    toc_.offset_unit = OffsetUnit::CueBytes;

    Track& lead_out = toc_.tracks[last];
    lead_out.number = TrackNumber{static_cast<std::uint8_t>(last + 1)};
    lead_out.type = TrackType::Cdda;
    lead_out.start = toc_.end;
    lead_out.end = toc_.end;
    return {};
}

Ex<void> DiscEngine::mount_iso(std::string_view iso_path) {

    if (policy_.iso_sizing == CuePolicy::IsoSizing::Declared2048 &&
        !ends_with_ci(iso_path, ".iso")) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    cdda_order_ = CddaOrder::LittleEndian;

    auto f = open_file(iso_path);
    if (!f) return std::unexpected(f.error());
    auto sz = (*f)->size();
    if (!sz) return std::unexpected(sz.error());

    if (policy_.iso_sizing == CuePolicy::IsoSizing::Declared2048) {
        return declare_iso_2048(std::move(*f), *sz);
    }

    struct Probe {
        std::uint32_t sector_size;
        bool mode2;
    };
    constexpr Probe kProbes[] = {
        {2048, false},
        {2352, false},
        {kMode2Cooked, true},
        {2352, true},
    };

    for (const Probe& p : kProbes) {

        std::uint64_t seek = 16ull * p.sector_size;
        if (p.sector_size == 2352 && !p.mode2) seek += 16;
        if (p.mode2) seek += 24;

        std::byte pvd[kUserDataBytes]{};
        auto rd = (*f)->read_at(seek, std::span<std::byte>(pvd, sizeof(pvd)));
        if (!rd || *rd != sizeof(pvd)) continue;

        const auto* b = reinterpret_cast<const unsigned char*>(pvd);
        const bool iso9660 = b[0] == 1 && std::memcmp(b + 1, "CD001", 5) == 0 && b[6] == 1;
        const bool high_sierra = b[8] == 1 && std::memcmp(b + 9, "CDROM", 5) == 0 && b[14] == 1;
        if (!iso9660 && !high_sierra) continue;

        Track& t = toc_.tracks[0];
        t.number = TrackNumber{1};
        t.type = p.mode2 ? TrackType::Mode2 : TrackType::Mode1;
        t.sector_size = p.sector_size;
        t.start = Lba{0};
        t.end = Lba{static_cast<std::uint32_t>(sz->v / p.sector_size)};
        t.offset = 0;
        t.backing = 0;

        files_[0] = std::move(*f);
        file_count_ = 1;

        toc_.last = 1;
        toc_.end = t.end;
        toc_.sector_size = p.sector_size;
        toc_.offset_unit = OffsetUnit::CueBytes;

        Track& lead_out = toc_.tracks[1];
        lead_out.number = TrackNumber{2};
        lead_out.type = TrackType::Cdda;
        lead_out.start = t.end;
        lead_out.end = t.end;
        return {};
    }
    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
}

Ex<void> DiscEngine::declare_iso_2048(std::unique_ptr<IFile> f, FileSize sz) {
    constexpr std::uint32_t kDeclared = 2048;

    Track& t = toc_.tracks[0];
    t.number = TrackNumber{1};
    t.type = TrackType::Mode1;
    t.sector_size = kDeclared;
    t.start = Lba{0};
    t.end = Lba{static_cast<std::uint32_t>(sz.v / kDeclared)};
    t.offset = 0;
    t.backing = 0;

    files_[0] = std::move(f);
    file_count_ = 1;

    toc_.last = 1;
    toc_.end = t.end;
    toc_.sector_size = kDeclared;
    toc_.offset_unit = OffsetUnit::CueBytes;

    Track& lead_out = toc_.tracks[1];
    lead_out.number = TrackNumber{2};
    lead_out.type = TrackType::Cdda;
    lead_out.start = t.end;
    lead_out.end = t.end;
    return {};
}

Ex<void> DiscEngine::mount_chd(std::unique_ptr<IChdSource> source,
                               std::unique_ptr<IChdSource> prefetch_source) {
    if (!source) return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});
    (void)unmount();

    const IChdSource::Geometry geo = source->geometry();
    if (geo.hunk_bytes == 0 || geo.hunk_bytes < geo.unit_bytes) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), geo.hunk_bytes});
    }

    if (geo.unit_bytes != static_cast<std::uint32_t>(kCdFrameSize)) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), geo.unit_bytes});
    }
    sectors_per_hunk_ = geo.hunk_bytes / geo.unit_bytes;
    hunk_bytes_ = geo.hunk_bytes;
    hunk_count_ = geo.hunk_count;

    hunk_buf_ = std::make_unique<std::byte[]>(hunk_bytes_);
    hunk_memo_ = kNoHunk;
    hunk_ptr_ = nullptr;
    chd_ = std::move(source);

    if (prefetch_ != nullptr && prefetch_source != nullptr) {
        if (auto a = prefetch_->attach(std::move(prefetch_source),
                                       static_cast<std::uint32_t>(hunk_bytes_), hunk_count_);
            !a) {
            ++prefetch_refusals_;
        } else {
            prefetch_depth_ = prefetch_->depth();
        }
    }

    std::uint32_t sector_cnt = 0;
    std::uint32_t count = 0;
    for (; count < kMaxCueTracks; ++count) {
        const auto row = chd_->track_metadata(count);
        if (!row) break;
        ChdTrackMeta m;
        if (!parse_chd_meta(*row, m)) break;

        Track& t = toc_.tracks[count];
        t.number = TrackNumber{static_cast<std::uint8_t>(count + 1)};

        const bool pregap_valid = (m.pgtype[0] == 'V');
        std::uint32_t pregap = m.pregap;
        if (count != 0) {
            Track& prev = toc_.tracks[count - 1];
            if (!pregap_valid) prev.end = Lba{prev.end.v + pregap};
            toc_.end = prev.end;
            t.start = toc_.end;
            if (pregap_valid) t.start = Lba{t.start.v + pregap};
        } else {
            t.start = Lba{pregap_valid ? pregap : 0u};
        }

        t.pregap = pregap;
        t.index_num = 2;
        if (!pregap_valid) pregap = 0;

        t.sector_size = 0;
        t.type = TrackType::Cdda;
        for (const ChdTypeRow& row_f : kChdTypes) {
            if (row_f.name == std::string_view(m.type)) {
                t.sector_size = row_f.sector_size;
                t.type = row_f.type;
                break;
            }
        }

        const std::string_view subtype(m.subtype);
        if (subtype == "RW" || subtype == "RW_RAW") {

            if (!toc_.has_subcode && subtype == "RW") {
                sub_shaper_ = std::make_unique<RwInterleaver>();
            }
            toc_.has_subcode = true;
        }

        t.offset = static_cast<std::int64_t>(sector_cnt) + static_cast<std::int64_t>(pregap) -
                   static_cast<std::int64_t>(t.start.v);
        t.end = Lba{t.start.v + m.frames - pregap};
        toc_.end = Lba{t.end.v + m.postgap};
        sector_cnt += ((m.frames + static_cast<std::uint32_t>(kChdTrackPadding) - 1) /
                       static_cast<std::uint32_t>(kChdTrackPadding)) *
                      static_cast<std::uint32_t>(kChdTrackPadding);
    }

    if (count == 0) {
        (void)unmount();
        return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});
    }

    toc_.last = static_cast<std::uint8_t>(count);
    toc_.sector_size = toc_.tracks[0].sector_size;

    cdda_order_ = CddaOrder::BigEndian;

    toc_.offset_unit = OffsetUnit::ChdSectors;

    if (policy_.post_load_shift == CuePolicy::PostLoadTocShift::FakeLeadInPregap150) {
        const auto lead_in = static_cast<std::uint32_t>(kRedBookPregap);
        for (std::uint32_t i = 0; i < count; ++i) {
            Track& t = toc_.tracks[i];
            if (i == 0) {

                t.pregap = lead_in;
                t.start = Lba{lead_in};
                t.end = Lba{t.end.v + lead_in};
            } else {

                const std::uint32_t frame_cnt = (t.end.v - t.start.v) + t.pregap;
                t.start = toc_.tracks[i - 1].end;
                t.end = Lba{t.start.v + frame_cnt};
            }
            t.offset -= static_cast<std::int64_t>(lead_in);
        }
        toc_.end = toc_.tracks[count - 1].end;
    }

    if (policy_.post_load_shift == CuePolicy::PostLoadTocShift::Uniform150) {
        const auto lead_in = static_cast<std::uint32_t>(kRedBookPregap);
        for (std::uint32_t i = 0; i < count; ++i) {
            Track& t = toc_.tracks[i];
            t.start = Lba{t.start.v + lead_in};
            t.end = Lba{t.end.v + lead_in};
            t.offset -= static_cast<std::int64_t>(lead_in);
        }
        toc_.end = Lba{toc_.end.v + lead_in};
    }

    Track& lead_out = toc_.tracks[count];
    lead_out.number = TrackNumber{static_cast<std::uint8_t>(count + 1)};
    lead_out.type = TrackType::Cdda;
    lead_out.start = toc_.end;
    lead_out.end = toc_.end;

    mounted_ = true;
    return {};
}

std::optional<TrackIndex> DiscEngine::track_for_lba(Lba lba) const {
    return svc::track_for_lba(toc_, lba);
}

std::optional<TrackIndex> DiscEngine::source_track(Lba lba) const {
    const auto ti = track_for_lba(lba);
    if (!ti) return std::nullopt;

    if (chd_) return ti;

    if (policy_.pregap_model == CuePolicy::PregapModel::IndexPair) return ti;
    const Track& t = toc_.tracks[ti->v];

    if (lba < t.start && t.number.v > 1) {
        return TrackIndex{static_cast<std::uint8_t>(t.number.v - 2)};
    }
    return ti;
}

Ex<void> DiscEngine::load_hunk(std::uint32_t hunk) {

    if (prefetch_ != nullptr) {
        if (const std::byte* p = prefetch_->take(hunk); p != nullptr) {
            hunk_ptr_ = p;
            return {};
        }
    }
    if (hunk == hunk_memo_) {
        hunk_ptr_ = hunk_buf_.get();
        return {};
    }

    ++sync_decompress_;
    if (prefetch_ != nullptr) prefetch_->note_miss();
    auto r = chd_->read_hunk(hunk, std::span<std::byte>(hunk_buf_.get(), hunk_bytes_));
    if (!r) {
        hunk_memo_ = kNoHunk;
        hunk_ptr_ = nullptr;
        return std::unexpected(r.error());
    }
    hunk_memo_ = hunk;
    hunk_ptr_ = hunk_buf_.get();
    return {};
}

Ex<std::size_t> DiscEngine::read_form(Lba lba, std::span<std::byte> dst, Form form) {
    if (!mounted_) return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});

    const auto ti = source_track(lba);
    if (!ti) return std::unexpected(Error{Errc::not_found, ERR_SITE(), lba.v});
    const Track& t = toc_.tracks[ti->v];

    const std::uint32_t ss = t.sector_size;
    if (ss == 0) {

        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), ti->v});
    }

    std::size_t out_len = 0;
    std::size_t dst_off = 0;
    std::uint32_t s_off = 0;
    std::size_t len = 0;

    switch (form) {
        case Form::Native:
            out_len = ss;
            len = ss;
            break;
        case Form::UserData:

            out_len = kUserDataBytes;
            len = kUserDataBytes;
            if (ss != kUserDataBytes) s_off = (t.type == TrackType::Mode2) ? 24u : 16u;
            if (static_cast<std::size_t>(s_off) + len > ss) {
                return std::unexpected(Error{Errc::bad_format, ERR_SITE(), ss});
            }
            break;
        case Form::RedBook:

            out_len = kCdDataSize;
            if (ss == kCdDataSize) {
                len = kCdDataSize;
            } else if (ss == kMode2Cooked) {
                dst_off = 16;
                len = kMode2Cooked;
            } else if (ss == kUserDataBytes) {
                dst_off = 16;
                len = kUserDataBytes;
            } else {
                return std::unexpected(Error{Errc::bad_format, ERR_SITE(), ss});
            }
            break;
    }

    if (dst.size() < out_len) {
        return std::unexpected(
            Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(out_len)});
    }

    if (form == Form::RedBook && ss != kCdDataSize) {

        std::memset(dst.data(), 0, kCdDataSize);
        if (ss == kUserDataBytes) {

            auto* b = reinterpret_cast<unsigned char*>(dst.data());
            b[0] = 0x00;
            std::memset(b + 1, 0xff, 10);
            b[11] = 0x00;
            const std::uint32_t f_lba = lba.v + static_cast<std::uint32_t>(kRedBookPregap);
            b[12] = bcd(f_lba / (kFramesPerSecond * 60));
            b[13] = bcd((f_lba / kFramesPerSecond) % 60);
            b[14] = bcd(f_lba % kFramesPerSecond);
            b[15] = 0x01;
        }
    }

    if (chd_) {

        const std::int64_t chd_lba64 = static_cast<std::int64_t>(lba.v) + t.offset;
        if (chd_lba64 < 0) return std::unexpected(Error{Errc::bad_format, ERR_SITE(), lba.v});
        const std::uint32_t chd_lba = static_cast<std::uint32_t>(chd_lba64);
        const std::uint32_t hunk = chd_lba / sectors_per_hunk_;
        const std::uint32_t frame = chd_lba % sectors_per_hunk_;
        auto h = load_hunk(hunk);
        if (!h) return std::unexpected(h.error());

        const std::size_t base = static_cast<std::size_t>(frame) * kCdFrameSize + s_off;
        if (base + len > hunk_bytes_) {
            return std::unexpected(
                Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(base)});
        }
        if (hunk_ptr_ == nullptr) {
            return std::unexpected(Error{Errc::io, ERR_SITE(), hunk});
        }
        std::memcpy(dst.data() + dst_off, hunk_ptr_ + base, len);
        normalize_cdda_(t, form, dst.subspan(dst_off, len));
        return out_len;
    }

    IFile* f = (t.backing < file_count_) ? files_[t.backing].get() : nullptr;
    if (f == nullptr) return std::unexpected(Error{Errc::not_found, ERR_SITE(), t.backing});

    const std::int64_t pos = static_cast<std::int64_t>(lba.v) * static_cast<std::int64_t>(ss) -
                             t.offset + static_cast<std::int64_t>(s_off);
    if (pos < 0) return std::unexpected(Error{Errc::bad_format, ERR_SITE(), lba.v});
    auto rd = f->read_at(static_cast<std::uint64_t>(pos), dst.subspan(dst_off, len));
    if (!rd) return std::unexpected(rd.error());
    if (*rd != len) {
        return std::unexpected(
            Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(*rd)});
    }
    return out_len;
}

Ex<std::size_t> DiscEngine::read_sector(Lba lba, std::span<std::byte> dst) {
    return read_form(lba, dst, Form::Native);
}

Ex<std::size_t> DiscEngine::read_user_data(Lba lba, std::span<std::byte> dst) {
    return read_form(lba, dst, Form::UserData);
}

void DiscEngine::normalize_cdda_(const Track& t, Form form,
                                 std::span<std::byte> frame) const noexcept {

    if (form != Form::RedBook) return;
    if (t.type != TrackType::Cdda) return;
    if (cdda_order_ == kWireCddaOrder) return;
    for (std::size_t i = 0; i + 1 < frame.size(); i += 2) {
        const std::byte lo = frame[i];
        frame[i] = frame[i + 1];
        frame[i + 1] = lo;
    }
}

Ex<std::size_t> DiscEngine::read_raw_frame(Lba lba, std::span<std::byte> dst) {
    return read_form(lba, dst, Form::RedBook);
}

Ex<std::size_t> DiscEngine::read_subcode(Lba lba, std::span<std::byte> dst) {
    if (!mounted_) return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});
    if (dst.size() < kSubcodeBytes) {
        return std::unexpected(
            Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(dst.size())});
    }

    if (sub_) {

        const std::uint32_t at = lba.v > sub_origin_ ? lba.v - sub_origin_ : 0u;
        if (sub_cdg_) return read_cdg_subcode_(at, dst);
        auto rd = sub_->read_at(static_cast<std::uint64_t>(at) * kSubcodeBytes,
                                dst.subspan(0, kSubcodeBytes));
        if (!rd) return std::unexpected(rd.error());
        if (*rd != kSubcodeBytes) {
            return std::unexpected(
                Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(*rd)});
        }
        return kSubcodeBytes;
    }

    if (chd_) {
        if (!toc_.has_subcode) return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});

        const auto ti = source_track(lba);
        if (!ti) return std::unexpected(Error{Errc::not_found, ERR_SITE(), lba.v});
        const std::int64_t chd_lba64 = static_cast<std::int64_t>(lba.v) + toc_.tracks[ti->v].offset;
        if (chd_lba64 < 0) return std::unexpected(Error{Errc::bad_format, ERR_SITE(), lba.v});
        const std::uint32_t chd_lba = static_cast<std::uint32_t>(chd_lba64);
        auto h = load_hunk(chd_lba / sectors_per_hunk_);
        if (!h) return std::unexpected(h.error());

        const std::size_t base =
            static_cast<std::size_t>(chd_lba % sectors_per_hunk_) * kCdFrameSize + kCdDataSize;
        if (base + kSubcodeBytes > hunk_bytes_) {
            return std::unexpected(
                Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(base)});
        }
        if (hunk_ptr_ == nullptr) {
            return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
        }
        std::memcpy(dst.data(), hunk_ptr_ + base, kSubcodeBytes);
        return kSubcodeBytes;
    }

    return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
}

Ex<std::size_t> DiscEngine::read_full_frame(Lba lba, std::span<std::byte> dst) {
    if (dst.size() < kCdFrameSize) {
        return std::unexpected(
            Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(dst.size())});
    }
    const std::span<std::byte, kCdFrameSize> frame = dst.first<kCdFrameSize>();
    auto r = read_raw_frame(lba, frame.first<kCdDataSize>());
    if (!r) return std::unexpected(r.error());
    const std::span<std::byte> tail = frame.subspan(kCdDataSize, kSubcodeBytes);
    std::memset(tail.data(), 0, tail.size());

    if (toc_.has_subcode) {
        if (!read_subcode(lba, tail)) {
            std::memset(tail.data(), 0, tail.size());
        } else if (sub_shaper_ != nullptr) {
            sub_shaper_->shape(lba, frame);
        }
    }
    if (frame_shaper_ != nullptr) frame_shaper_->shape(lba, frame);
    return kCdFrameSize;
}

constexpr int kCdgColumnOffset[24] = {0,       18 - 24,  5 - 48,   23 - 72,  4 - 96,   2 - 120,
                                      6 - 144, 7 - 168,  8,        9 - 24,   10 - 48,  11 - 72,
                                      12 - 96, 13 - 120, 14 - 144, 15 - 168, 16,       17 - 24,
                                      1 - 48,  19 - 72,  20 - 96,  21 - 120, 22 - 144, 3 - 168};

constexpr std::uint32_t kCdgReach = 165;
constexpr std::size_t kCdgSpan = 254;

Ex<std::size_t> DiscEngine::read_cdg_subcode_(std::uint32_t at, std::span<std::byte> dst) {

    const std::uint64_t want = static_cast<std::uint64_t>(at) * kSubcodeBytes;
    const std::uint64_t base = want > kCdgReach ? want - kCdgReach : 0;
    std::byte buf[kCdgSpan]{};
    auto rd = sub_->read_at(base, std::span<std::byte>(buf, sizeof(buf)));
    if (!rd) return std::unexpected(rd.error());
    for (std::size_t pack = 0; pack < 4; ++pack) {
        for (std::size_t col = 0; col < 24; ++col) {
            const int rel =
                static_cast<int>(pack * 24) + kCdgColumnOffset[col] + static_cast<int>(kCdgReach);
            dst[pack * 24 + col] = buf[static_cast<std::size_t>(rel)];
        }
    }
    return kSubcodeBytes;
}

void DiscEngine::open_sub_or_cdg_(std::string_view cue_path) {

    if (cue_path.size() < 4) return;
    std::string name(cue_path.substr(0, cue_path.size() - 4));
    name.append(".sub");
    auto s = open_file(name);
    bool cdg = false;
    if (!s) {
        name.replace(name.size() - 4, 4, ".cdg");
        s = open_file(name);
        cdg = true;
    }
    if (!s) return;
    sub_ = std::move(*s);
    sub_cdg_ = cdg;
    sub_origin_ = static_cast<std::uint32_t>(kRedBookPregap);
    toc_.has_subcode = true;
    if (!cdg) sub_shaper_ = std::make_unique<RwInterleaver>();
}

std::uint32_t DiscEngine::seek_ms(Lba from_lba, Lba to_lba) const {
    return svc::seek_ms(from_lba, to_lba);
}

void DiscEngine::advise_ahead(Lba lba) noexcept {

    if (chd_ || !mounted_) return;
    const auto ti = source_track(lba);
    if (!ti) return;
    const Track& t = toc_.tracks[ti->v];
    if (t.sector_size == 0) return;
    IFile* const f = (t.backing < file_count_) ? files_[t.backing].get() : nullptr;
    if (f == nullptr) return;
    const std::int64_t pos =
        static_cast<std::int64_t>(lba.v) * static_cast<std::int64_t>(t.sector_size) - t.offset;
    if (pos < 0) return;
    const std::uint64_t len = static_cast<std::uint64_t>(kAdviseAheadSectors) * t.sector_size;

    (void)f->advise(IFile::Access::WillNeed, static_cast<std::uint64_t>(pos), len);
}

void DiscEngine::prefetch_hint(Lba lba) {

    if (prefetch_ == nullptr) return;
    if (!chd_ || sectors_per_hunk_ == 0) return;

    const auto ti = source_track(lba);
    if (!ti) return;
    const std::int64_t chd_lba = static_cast<std::int64_t>(lba.v) + toc_.tracks[ti->v].offset;
    if (chd_lba < 0) return;
    const std::uint32_t hunk = static_cast<std::uint32_t>(chd_lba) / sectors_per_hunk_;
    if (hunk_count_ != 0 && hunk >= hunk_count_) return;
    prefetch_->submit(hunk);
}

#if defined(TASTY_HAVE_LIBCHDR)
namespace {

struct CoreFileBinding {
    core_file cf{};
    IFile* file = nullptr;
    std::uint64_t pos = 0;
    std::uint64_t size = 0;
};

std::uint64_t cf_fsize(core_file* f) {
    auto* b = static_cast<CoreFileBinding*>(f->argp);
    return b->size;
}

std::size_t cf_fread(void* dst, std::size_t elem, std::size_t count, core_file* f) {
    auto* b = static_cast<CoreFileBinding*>(f->argp);
    const std::uint64_t bytes = static_cast<std::uint64_t>(elem) * count;
    if (elem == 0 || bytes == 0) return 0;
    auto rd = b->file->read_at(b->pos, std::span<std::byte>(static_cast<std::byte*>(dst),
                                                            static_cast<std::size_t>(bytes)));
    if (!rd) return 0;
    b->pos += *rd;
    return *rd / elem;
}

int cf_fclose(core_file*) { return 0; }

int cf_fseek(core_file* f, std::int64_t off, int whence) {
    auto* b = static_cast<CoreFileBinding*>(f->argp);
    std::int64_t base = 0;
    if (whence == SEEK_CUR)
        base = static_cast<std::int64_t>(b->pos);
    else if (whence == SEEK_END)
        base = static_cast<std::int64_t>(b->size);
    const std::int64_t next = base + off;
    if (next < 0) return -1;
    b->pos = static_cast<std::uint64_t>(next);
    return 0;
}

class LibChdrSource final : public IChdSource {
public:
    LibChdrSource(std::unique_ptr<IFile> file, std::uint64_t size) : file_(std::move(file)) {
        binding_.cf.argp = &binding_;
        binding_.cf.fsize = &cf_fsize;
        binding_.cf.fread = &cf_fread;
        binding_.cf.fclose = &cf_fclose;
        binding_.cf.fseek = &cf_fseek;
        binding_.file = file_.get();
        binding_.size = size;
    }
    ~LibChdrSource() override {
        if (chd_ != nullptr) chd_close(chd_);
    }
    LibChdrSource(const LibChdrSource&) = delete;
    LibChdrSource& operator=(const LibChdrSource&) = delete;

    Ex<void> open() {
        const chd_error err = chd_open_core_file(&binding_.cf, CHD_OPEN_READ, nullptr, &chd_);
        if (err != CHDERR_NONE) {
            chd_ = nullptr;
            return std::unexpected(
                Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(err)});
        }
        header_ = chd_get_header(chd_);
        if (header_ == nullptr) {
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
        }
        return {};
    }

    Geometry geometry() const noexcept override {
        return Geometry{header_->hunkbytes, header_->unitbytes, header_->totalhunks};
    }

    std::optional<ChdMetaRow> track_metadata(std::uint32_t index) override {

        if (chd_get_metadata(chd_, CDROM_TRACK_METADATA2_TAG, index, meta_, sizeof(meta_), nullptr,
                             nullptr, nullptr) == CHDERR_NONE) {
            meta_[sizeof(meta_) - 1] = '\0';
            return ChdMetaRow{std::string_view(meta_), true};
        }
        if (chd_get_metadata(chd_, CDROM_TRACK_METADATA_TAG, index, meta_, sizeof(meta_), nullptr,
                             nullptr, nullptr) == CHDERR_NONE) {
            meta_[sizeof(meta_) - 1] = '\0';
            return ChdMetaRow{std::string_view(meta_), false};
        }
        return std::nullopt;
    }

    Ex<void> read_hunk(std::uint32_t hunk, std::span<std::byte> dst) override {
        if (dst.size() < header_->hunkbytes) {
            return std::unexpected(
                Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(dst.size())});
        }
        const chd_error err = chd_read(chd_, hunk, dst.data());
        if (err != CHDERR_NONE) {
            return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(err)});
        }
        return {};
    }

private:
    std::unique_ptr<IFile> file_;
    CoreFileBinding binding_{};
    chd_file* chd_ = nullptr;
    const chd_header* header_ = nullptr;
    char meta_[512]{};
};

}  // namespace

Ex<void> DiscEngine::mount_chd_path(std::string_view chd_path) {
    auto f = open_file(chd_path);
    if (!f) return std::unexpected(f.error());
    auto sz = (*f)->size();
    if (!sz) return std::unexpected(sz.error());

    IFile* const primary_raw = f->get();
    auto src = std::make_unique<LibChdrSource>(std::move(*f), sz->v);
    auto opened = src->open();
    if (!opened) return std::unexpected(opened.error());

    std::unique_ptr<IChdSource> second;
    if (prefetch_ != nullptr) {
        if (auto f2 = open_file(chd_path)) {
            auto sz2 = (*f2)->size();
            if (sz2) {
                auto s2 = std::make_unique<LibChdrSource>(std::move(*f2), sz2->v);
                if (auto o2 = s2->open(); o2) second = std::move(s2);
            }
        }
    }
    auto mounted = mount_chd(std::move(src), std::move(second));
    if (!mounted) return mounted;

    const std::size_t depth = (prefetch_depth_ != 0) ? prefetch_depth_ : kPrefetchMinDepth;
    (void)primary_raw->advise(IFile::Access::WillNeed, 0,
                              static_cast<std::uint64_t>(hunk_bytes_) * depth);
    return {};
}
#endif

}  // namespace mister::svc
