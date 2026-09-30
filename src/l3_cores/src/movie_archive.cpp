// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/movie_archive.h"

#include <algorithm>
#include <cerrno>
#include <optional>
#include <vector>

#include "cores/movie_codec.h"
#include "infra/crc32.h"
#include "svc/archive.h"
#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::cores {
namespace {

using Use = MovieArchiveFormat::Use;
using CR = IMovieCodec::Refusal;

constexpr std::string_view kBom = "\xEF\xBB\xBF";

std::unexpected<Error> refuse(CR r, std::uint16_t site) {
    return std::unexpected(Error{Errc::bad_format, site, static_cast<std::uint32_t>(r)});
}

std::unexpected<Error> lower(const Error& e) {
    if (e.code == Errc::bad_format || e.code == Errc::stale) return refuse(CR::Archive, e.site);
    return std::unexpected(e);
}

bool is_sep(char c) noexcept { return c == '/' || c == '\\'; }

bool printable(char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    return u >= 0x20 && u != 0x7F;
}

bool all_printable(std::string_view s) noexcept { return std::ranges::all_of(s, printable); }

std::size_t common_dir(std::span<const svc::ArchiveEntry> es) noexcept {
    if (es.empty()) return 0;
    std::string_view p = es.front().name;
    for (const svc::ArchiveEntry& e : es) {
        std::size_t n = 0;
        while (n < p.size() && n < e.name.size() && p[n] == e.name[n])
            ++n;
        p = p.substr(0, n);
    }
    return !p.empty() && is_sep(p.back()) ? p.size() : 0;
}

struct Named {
    std::string stem;
    bool zst = false;
    bool unlisted = false;
};

Named name_of(std::string_view name, std::size_t cut, MovieArchiveFormat::Naming naming) {
    if (!name.empty() && is_sep(name.back())) return {{}, false, true};
    if (naming == MovieArchiveFormat::Naming::Exact) return {std::string(name), false, false};
    name.remove_prefix(cut);
    const bool zst = name.ends_with(".zst");
    std::string stem(name.substr(0, name.find('.')));
    std::ranges::replace(stem, '\\', '/');
    return {std::move(stem), zst, false};
}

std::optional<Use> rule_for(const MovieArchiveFormat& f, std::string_view stem) noexcept {
    for (const MovieArchiveFormat::Rule& r : f.rules) {
        if (r.stem == stem) return r.use;
    }
    return std::nullopt;
}

bool readable(const svc::ArchiveEntry& e) noexcept {
    return !e.encrypted && e.plausible && (e.method == 0 || e.method == 8);
}

[[nodiscard]] Ex<std::string> read_whole(const svc::IArchive& a, std::size_t index) {
    const svc::ArchiveEntry& e = a.entries()[index];
    auto f = a.open(index);
    if (!f) return lower(f.error());
    std::string out(static_cast<std::size_t>(e.size), '\0');
    std::size_t done = 0;
    while (done < out.size()) {
        const auto got = (*f)->read_at(done, std::as_writable_bytes(std::span(out).subspan(done)));
        if (!got) return lower(got.error());
        if (*got == 0) return refuse(CR::Archive, ERR_SITE());
        done += *got;
    }
    const auto bytes = std::as_bytes(std::span(out));
    const std::span<const std::uint8_t> u(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                          bytes.size());
    if (crc32_update(0, u) != e.crc) return refuse(CR::Archive, ERR_SITE());
    return out;
}

std::string_view strip_bom(std::string_view s) noexcept {
    if (s.starts_with(kBom)) s.remove_prefix(kBom.size());
    return s;
}

std::string short_lines(std::string_view text) {
    std::string out;
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        if (line.ends_with('\r')) line.remove_suffix(1);
        if (line.size() <= IMovieCodec::kLineMax) out.append(line).append(1, '\n');
    }
    return out;
}

class Flattener {
public:
    Flattener(std::string_view in, std::string_view prefix, std::size_t cap)
        : in_(in), path_(prefix), cap_(cap) {}

    [[nodiscard]] Ex<std::string> run() {
        ws_();
        if (pos_ < in_.size()) {
            if (!value_(0)) return refuse(over_ ? CR::TooLong : CR::Archive, ERR_SITE());
            ws_();
            if (pos_ != in_.size()) return refuse(CR::Archive, ERR_SITE());
        }
        return std::move(out_);
    }

private:
    void ws_() noexcept {
        while (pos_ < in_.size() &&
               (in_[pos_] == ' ' || in_[pos_] == '\t' || in_[pos_] == '\n' || in_[pos_] == '\r'))
            ++pos_;
    }
    bool eat_(char c) noexcept {
        ws_();
        if (pos_ < in_.size() && in_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    std::optional<std::string_view> string_() noexcept {
        if (!eat_('"')) return std::nullopt;
        const std::size_t start = pos_;
        while (pos_ < in_.size()) {
            const char c = in_[pos_];
            if (static_cast<unsigned char>(c) < 0x20) return std::nullopt;
            if (c == '\\') {
                pos_ += 2;
                continue;
            }
            if (c == '"') {
                const std::string_view s = in_.substr(start, pos_ - start);
                ++pos_;
                return s;
            }
            ++pos_;
        }
        return std::nullopt;
    }
    std::optional<std::string_view> scalar_() noexcept {
        ws_();
        const std::size_t start = pos_;
        for (const std::string_view w : {"true", "false", "null"}) {
            if (in_.substr(pos_).starts_with(w)) {
                pos_ += w.size();
                return in_.substr(start, w.size());
            }
        }
        while (pos_ < in_.size() &&
               (std::string_view("+-.0123456789eE").find(in_[pos_]) != std::string_view::npos))
            ++pos_;
        if (pos_ == start) return std::nullopt;
        return in_.substr(start, pos_ - start);
    }
    bool emit_(std::string_view v) {
        if (path_.size() + 2 + v.size() > IMovieCodec::kLineMax) return true;
        if (!all_printable(path_) || path_.find(' ') != std::string::npos || !all_printable(v))
            return true;
        out_.append(path_).append(1, ' ').append(v).append(1, '\n');
        over_ = out_.size() > cap_;
        return !over_;
    }
    bool value_(std::size_t depth) {
        ws_();
        if (pos_ >= in_.size()) return false;
        const char c = in_[pos_];
        if (c == '{' || c == '[') {
            if (depth == MovieArchiveFormat::kJsonDepth) return false;
            const bool obj = c == '{';
            ++pos_;
            if (eat_(obj ? '}' : ']')) return true;
            for (std::size_t i = 0;; ++i) {
                const std::size_t keep = path_.size();
                if (obj) {
                    const auto k = string_();
                    if (!k || !eat_(':')) return false;
                    path_.append(1, '.').append(*k);
                } else {
                    path_.append(1, '.').append(std::to_string(i));
                }
                if (!value_(depth + 1)) return false;
                path_.resize(keep);
                if (eat_(',')) continue;
                return eat_(obj ? '}' : ']');
            }
        }
        const auto v = c == '"' ? string_() : scalar_();
        return v && emit_(*v);
    }

    std::string_view in_;
    std::string path_;
    std::size_t cap_;
    std::string out_;
    std::size_t pos_ = 0;
    bool over_ = false;
};

class MovieText final : public svc::IFile {
public:
    MovieText(std::unique_ptr<svc::IArchive> archive, std::string head,
              std::unique_ptr<svc::IFile> log, const svc::ArchiveEntry& entry)
        : archive_(std::move(archive)), head_(std::move(head)), log_(std::move(log)),
          member_size_(entry.size), crc_want_(entry.crc) {}

    [[nodiscard]] Ex<void> start() {
        std::array<std::byte, 3> b{};
        const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(3, member_size_));
        const auto got = log_->read_at(0, std::span(b).first(want));
        if (!got) return lower(got.error());
        if (*got != want) return refuse(CR::Archive, ERR_SITE());
        account_(0, std::span<const std::byte>(b).first(want));
        if (!crc_ok_()) return refuse(CR::Archive, ERR_SITE());
        if (want == 3 && std::string_view(reinterpret_cast<const char*>(b.data()), 3) == kBom)
            base_ = 3;
        return {};
    }

    [[nodiscard]] Ex<std::size_t> read_at(std::uint64_t off, std::span<std::byte> dst) override {
        if (bad_) return refuse(CR::Archive, ERR_SITE());
        std::size_t done = 0;
        if (off < head_.size()) {
            const auto n =
                static_cast<std::size_t>(std::min<std::uint64_t>(dst.size(), head_.size() - off));
            std::copy_n(reinterpret_cast<const std::byte*>(head_.data()) + off, n, dst.data());
            done = n;
        }
        if (done == dst.size()) return done;
        const std::uint64_t at = off + done - head_.size() + base_;
        if (at >= member_size_) return done;
        const auto got = log_->read_at(at, dst.subspan(done));
        if (!got) return lower(got.error());
        account_(at, std::span<const std::byte>(dst.subspan(done, *got)));
        if (!crc_ok_()) {
            bad_ = true;
            return refuse(CR::Archive, ERR_SITE());
        }
        return done + *got;
    }

    [[nodiscard]] Ex<std::size_t> write_at(std::uint64_t, std::span<const std::byte>) override {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(EROFS)});
    }
    [[nodiscard]] Ex<svc::FileSize> size() const override {
        return svc::FileSize{head_.size() + (member_size_ - base_)};
    }
    [[nodiscard]] Ex<void> flush() override { return {}; }
    svc::FileKind kind() const noexcept override { return svc::FileKind::Zip; }
    [[nodiscard]] Ex<void> advise(Access a, std::uint64_t, std::uint64_t) override {
        return log_->advise(a, 0, 0);
    }

private:
    void account_(std::uint64_t at, std::span<const std::byte> b) noexcept {
        if (at > crc_pos_ || at + b.size() <= crc_pos_) return;
        b = b.subspan(static_cast<std::size_t>(crc_pos_ - at));
        crc_ = crc32_update(crc_, std::span<const std::uint8_t>(
                                      reinterpret_cast<const std::uint8_t*>(b.data()), b.size()));
        crc_pos_ += b.size();
    }
    bool crc_ok_() const noexcept { return crc_pos_ != member_size_ || crc_ == crc_want_; }

    std::unique_ptr<svc::IArchive> archive_;
    std::string head_;
    std::unique_ptr<svc::IFile> log_;
    std::uint64_t member_size_ = 0;
    std::uint64_t base_ = 0;
    std::uint32_t crc_want_ = 0;
    std::uint32_t crc_ = 0;
    std::uint64_t crc_pos_ = 0;
    bool bad_ = false;
};

[[nodiscard]] Ex<std::string> head_member(const svc::IArchive& a, std::size_t i,
                                          std::string_view stem, Use use, std::size_t room) {
    const svc::ArchiveEntry& e = a.entries()[i];
    switch (use) {
        case Use::Skip:
        case Use::Log:
            return std::string{};
        case Use::Lines: {
            if (e.size > MovieArchiveFormat::kLinesMax) return refuse(CR::TooLong, ERR_SITE());
            if (!readable(e)) return refuse(CR::Archive, ERR_SITE());
            const auto text = read_whole(a, i);
            if (!text) return std::unexpected(text.error());
            return short_lines(strip_bom(*text));
        }
        case Use::Json: {
            if (e.size > MovieArchiveFormat::kJsonMax) return refuse(CR::TooLong, ERR_SITE());
            if (!readable(e)) return refuse(CR::Archive, ERR_SITE());
            const auto text = read_whole(a, i);
            if (!text) return std::unexpected(text.error());
            return flatten_json(strip_bom(*text), stem, room);
        }
        case Use::Value:
            if (e.size <= MovieArchiveFormat::kValueMax && readable(e)) {
                const auto text = read_whole(a, i);
                if (!text) return std::unexpected(text.error());
                std::string_view v = strip_bom(*text);
                while (!v.empty() && (v.back() == '\n' || v.back() == '\r'))
                    v.remove_suffix(1);
                if (all_printable(v) && stem.size() + 1 + v.size() <= IMovieCodec::kLineMax)
                    return std::string(stem) + ' ' + std::string(v) + '\n';
            }
            [[fallthrough]];
        case Use::Presence:
            if (stem.size() + 1 > IMovieCodec::kLineMax) return std::string{};
            return '@' + std::string(stem) + '\n';
    }
    return std::string{};
}

}  // namespace

[[nodiscard]] Ex<std::string> flatten_json(std::string_view json, std::string_view prefix,
                                           std::size_t cap) {
    return Flattener(json, prefix, cap).run();
}

[[nodiscard]] Ex<std::unique_ptr<svc::IFile>> open_movie_archive(
    std::unique_ptr<svc::IArchive> archive, const MovieArchiveFormat& format) {
    if (archive == nullptr) return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    const std::span<const svc::ArchiveEntry> es = archive->entries();
    if (es.size() > MovieArchiveFormat::kMembersMax) return refuse(CR::TooLong, ERR_SITE());

    const std::size_t cut =
        format.naming == MovieArchiveFormat::Naming::BizHawkLump ? common_dir(es) : 0;
    std::vector<Named> names;
    names.reserve(es.size());
    std::optional<std::size_t> log;
    for (std::size_t i = 0; i < es.size(); ++i) {
        Named n = name_of(es[i].name, cut, format.naming);
        if (n.unlisted) {
            names.push_back(std::move(n));
            continue;
        }

        if (n.stem.empty() || !all_printable(n.stem)) {
            n.unlisted = true;
            names.push_back(std::move(n));
            continue;
        }
        for (const Named& m : names) {
            if (!m.unlisted && m.stem == n.stem && m.zst == n.zst)
                return refuse(CR::Archive, ERR_SITE());
        }
        const std::optional<Use> use = rule_for(format, n.stem);
        if (!n.zst && use == Use::Log) {
            if (log) return refuse(CR::Archive, ERR_SITE());
            log = i;
        }
        names.push_back(std::move(n));
    }

    for (Named& n : names) {
        if (!n.zst || n.unlisted) continue;
        const Use use = rule_for(format, n.stem).value_or(Use::Presence);
        if (use == Use::Skip || use == Use::Presence) continue;
        const bool plain = std::ranges::any_of(
            names, [&](const Named& m) { return !m.unlisted && !m.zst && m.stem == n.stem; });
        if (!plain) return refuse(CR::Archive, ERR_SITE());
        n.unlisted = true;
    }
    if (!log) return refuse(CR::NotAMovie, ERR_SITE());
    const svc::ArchiveEntry& le = es[*log];
    if (le.size > MovieArchiveFormat::kLogMax) return refuse(CR::TooLong, ERR_SITE());
    if (!readable(le)) return refuse(CR::Archive, ERR_SITE());

    std::string head;
    for (std::size_t i = 0; i < es.size(); ++i) {
        const Named& n = names[i];
        if (n.unlisted) continue;
        const Use use = n.zst ? Use::Presence : rule_for(format, n.stem).value_or(format.others);
        const auto text =
            head_member(*archive, i, n.stem, use, MovieArchiveFormat::kHeadMax - head.size());
        if (!text) return std::unexpected(text.error());
        head.append(*text);
        if (head.size() > MovieArchiveFormat::kHeadMax) return refuse(CR::TooLong, ERR_SITE());
    }

    auto f = archive->open(*log);
    if (!f) return lower(f.error());
    auto text = std::make_unique<MovieText>(std::move(archive), std::move(head), std::move(*f), le);
    if (auto r = text->start(); !r) return std::unexpected(r.error());
    return std::unique_ptr<svc::IFile>(std::move(text));
}

[[nodiscard]] Ex<std::unique_ptr<svc::IFile>> open_movie_archive(const svc::Vfs& vfs,
                                                                 std::string_view path,
                                                                 const MovieArchiveFormat& format) {
    auto a = vfs.open_archive(path, MovieArchiveFormat::kMembersMax);
    if (!a) {
        const Error e = a.error();
        if (e.code != Errc::bad_format) return std::unexpected(e);
        return refuse(e.detail == static_cast<std::uint32_t>(E2BIG) ? CR::TooLong : CR::Archive,
                      e.site);
    }
    return open_movie_archive(std::move(*a), format);
}

}  // namespace mister::cores
