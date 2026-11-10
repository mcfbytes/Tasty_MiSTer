// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/movie_archive.h"

#include <algorithm>
#include <cerrno>
#include <optional>
#include <vector>

#include "cores/json_flatten.h"
#include "cores/movie_codec.h"
#include "infra/byte_view.h"
#include "infra/crc32.h"
#include "svc/archive.h"
#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::cores {
namespace {

using Use = MovieArchiveFormat::Use;
using CR = MovieRefusal;

constexpr std::string_view kBom = "\xEF\xBB\xBF";

std::unexpected<Error> lower(const Error& e) {
    if (e.code == Errc::bad_format || e.code == Errc::stale)
        return std::unexpected(movie_refusal(CR::Archive, e.site));
    return std::unexpected(e);
}

bool is_sep(char c) noexcept {
    return c == '/' || c == '\\';
}

bool printable(char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    return u >= 0x20 && u != 0x7F;
}

bool all_printable(std::string_view s) noexcept {
    return std::ranges::all_of(s, printable);
}

std::size_t common_dir(std::span<const svc::ArchiveEntry> es) noexcept {
    if (es.empty())
        return 0;
    std::string_view p = es.front().name;
    for (const svc::ArchiveEntry& e : es) {
        std::size_t n = 0;
        while (n < p.size() && n < e.name.size() && p[n] == e.name[n])
            ++n;
        p = p.substr(0, n);
    }
    return !p.empty() && is_sep(p.back()) ? p.size() : 0;
}

struct Member {
    std::size_t index = 0;
    std::optional<std::string> stem;
    bool zst = false;
};

Member member_of(
    const svc::ArchiveEntry& e,
    std::size_t index,
    MovieArchiveFormat::Naming naming,
    std::size_t cut
) {
    Member m{.index = index, .stem = std::nullopt};
    if (!e.name.empty() && is_sep(e.name.back()))
        return m;
    if (naming == MovieArchiveFormat::Naming::Exact) {
        m.stem = e.name;
        return m;
    }
    std::string_view name = e.name;
    name.remove_prefix(cut);
    m.zst = name.ends_with(".zst");
    std::string stem(name.substr(0, name.find('.')));
    std::ranges::replace(stem, '\\', '/');
    m.stem = std::move(stem);
    return m;
}

std::optional<Use> rule_for(const MovieArchiveFormat& f, std::string_view stem) noexcept {
    for (const MovieArchiveFormat::Rule& r : f.rules) {
        if (r.stem == stem)
            return r.use;
    }
    return std::nullopt;
}

bool readable(const svc::ArchiveEntry& e) noexcept {
    return !e.encrypted && e.plausible && svc::method_of(e.method).has_value();
}

[[nodiscard]] Ex<std::string> read_whole(const svc::IArchive& a, std::size_t index) {
    const svc::ArchiveEntry& e = a.entries()[index];
    auto f = a.open(e);
    if (!f)
        return lower(f.error());
    std::string out(static_cast<std::size_t>(e.size), '\0');
    std::size_t done = 0;
    while (done < out.size()) {
        const auto got = (*f)->read_at(done, std::as_writable_bytes(std::span(out).subspan(done)));
        if (!got)
            return lower(got.error());
        if (*got == 0)
            return std::unexpected(movie_refusal(CR::Archive, ERR_SITE()));
        done += *got;
    }
    const auto bytes = std::as_bytes(std::span(out));
    const std::span<const std::uint8_t> u = infra::byte_cast<const std::uint8_t>(bytes);
    if (svc::crc32_extend({}, u) != e.crc)
        return std::unexpected(movie_refusal(CR::Archive, ERR_SITE()));
    return out;
}

std::string_view strip_bom(std::string_view s) noexcept {
    if (s.starts_with(kBom))
        s.remove_prefix(kBom.size());
    return s;
}

std::string short_lines(std::string_view text) {
    std::string out;
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        if (line.ends_with('\r'))
            line.remove_suffix(1);
        if (line.size() <= kMovieLineMax)
            out.append(line).append(1, '\n');
    }
    return out;
}

class MovieText final : public svc::IFile {
public:
    [[nodiscard]] static Ex<std::unique_ptr<svc::IFile>> open(
        std::unique_ptr<svc::IArchive> archive,
        std::string head,
        std::unique_ptr<svc::IFile> log,
        const svc::ArchiveEntry& entry
    ) {
        std::unique_ptr<MovieText> text(
            new MovieText(std::move(archive), std::move(head), std::move(log), entry)
        );
        if (auto r = text->prime_(); !r)
            return std::unexpected(r.error());
        return std::unique_ptr<svc::IFile>(std::move(text));
    }

    [[nodiscard]] Ex<std::size_t> read_at(std::uint64_t off, std::span<std::byte> dst) override {
        if (bad_)
            return std::unexpected(movie_refusal(CR::Archive, ERR_SITE()));
        std::size_t done = 0;
        if (off < head_.size()) {
            const auto n = static_cast<std::size_t>(
                std::min<std::uint64_t>(dst.size(), head_.size() - off)
            );
            std::copy_n(infra::text_bytes(head_).data() + off, n, dst.data());
            done = n;
        }
        if (done == dst.size())
            return done;
        const std::uint64_t at = off + done - head_.size() + base_;
        if (at >= member_size_)
            return done;
        const auto got = log_->read_at(at, dst.subspan(done));
        if (!got)
            return lower(got.error());
        account_(at, std::span<const std::byte>(dst.subspan(done, *got)));
        if (!crc_ok_()) {
            bad_ = true;
            return std::unexpected(movie_refusal(CR::Archive, ERR_SITE()));
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

    [[nodiscard]] Ex<void> advise(Access a, std::uint64_t, std::uint64_t) override {
        return log_->advise(a, 0, 0);
    }

private:
    MovieText(
        std::unique_ptr<svc::IArchive> archive,
        std::string head,
        std::unique_ptr<svc::IFile> log,
        const svc::ArchiveEntry& entry
    )
        : archive_(std::move(archive)),
          head_(std::move(head)),
          log_(std::move(log)),
          member_size_(entry.size),
          crc_want_(entry.crc) {}

    [[nodiscard]] Ex<void> prime_() {
        std::array<std::byte, 3> b{};
        const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(3, member_size_));
        const auto got = log_->read_at(0, std::span(b).first(want));
        if (!got)
            return lower(got.error());
        if (*got != want)
            return std::unexpected(movie_refusal(CR::Archive, ERR_SITE()));
        account_(0, std::span<const std::byte>(b).first(want));
        if (!crc_ok_())
            return std::unexpected(movie_refusal(CR::Archive, ERR_SITE()));
        if (want == 3 && infra::as_text(std::span(b).first(3)) == kBom)
            base_ = 3;
        return {};
    }

    void account_(std::uint64_t at, std::span<const std::byte> b) noexcept {
        if (at > crc_pos_ || at + b.size() <= crc_pos_)
            return;
        b = b.subspan(static_cast<std::size_t>(crc_pos_ - at));
        crc_ = svc::crc32_extend(crc_, infra::byte_cast<const std::uint8_t>(b));
        crc_pos_ += b.size();
    }

    bool crc_ok_() const noexcept { return crc_pos_ != member_size_ || crc_ == crc_want_; }

    std::unique_ptr<svc::IArchive> archive_;
    std::string head_;
    std::unique_ptr<svc::IFile> log_;
    std::uint64_t member_size_ = 0;
    std::uint64_t base_ = 0;
    svc::Crc32 crc_want_{};
    svc::Crc32 crc_{};
    std::uint64_t crc_pos_ = 0;
    bool bad_ = false;
};

[[nodiscard]] Ex<std::string> head_member(
    const svc::IArchive& a,
    std::size_t i,
    std::string_view stem,
    Use use,
    std::size_t room
) {
    const svc::ArchiveEntry& e = a.entries()[i];
    switch (use) {
        case Use::Skip:
        case Use::Log:
            return std::string{};
        case Use::Lines: {
            if (e.size > MovieArchiveFormat::kLinesMax)
                return std::unexpected(movie_refusal(CR::TooLong, ERR_SITE()));
            if (!readable(e))
                return std::unexpected(movie_refusal(CR::Archive, ERR_SITE()));
            const auto text = read_whole(a, i);
            if (!text)
                return std::unexpected(text.error());
            return short_lines(strip_bom(*text));
        }
        case Use::Json: {
            if (e.size > MovieArchiveFormat::kJsonMax)
                return std::unexpected(movie_refusal(CR::TooLong, ERR_SITE()));
            if (!readable(e))
                return std::unexpected(movie_refusal(CR::Archive, ERR_SITE()));
            const auto text = read_whole(a, i);
            if (!text)
                return std::unexpected(text.error());
            return flatten_json(strip_bom(*text), stem, JsonCap{room});
        }
        case Use::Value:
            if (e.size <= MovieArchiveFormat::kValueMax && readable(e)) {
                const auto text = read_whole(a, i);
                if (!text)
                    return std::unexpected(text.error());
                std::string_view v = strip_bom(*text);
                while (!v.empty() && (v.back() == '\n' || v.back() == '\r'))
                    v.remove_suffix(1);
                if (all_printable(v) && stem.size() + 1 + v.size() <= kMovieLineMax)
                    return std::string(stem) + ' ' + std::string(v) + '\n';
            }
            [[fallthrough]];
        case Use::Presence:
            if (stem.size() + 1 > kMovieLineMax)
                return std::string{};
            return '@' + std::string(stem) + '\n';
    }
    return std::string{};
}

}

[[nodiscard]] Ex<std::unique_ptr<svc::IFile>> open_movie_archive(
    std::unique_ptr<svc::IArchive> archive,
    const MovieArchiveFormat& format
) {
    if (archive == nullptr)
        return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    const std::span<const svc::ArchiveEntry> es = archive->entries();
    if (es.size() > MovieArchiveFormat::kMembersMax)
        return std::unexpected(movie_refusal(CR::TooLong, ERR_SITE()));

    const std::size_t cut = format.naming == MovieArchiveFormat::Naming::BizHawkLump
        ? common_dir(es)
        : 0;
    std::vector<Member> members;
    members.reserve(es.size());
    std::optional<std::size_t> log;
    for (std::size_t i = 0; i < es.size(); ++i) {
        Member m = member_of(es[i], i, format.naming, cut);

        if (m.stem && (m.stem->empty() || !all_printable(*m.stem)))
            m.stem.reset();
        if (!m.stem) {
            members.push_back(std::move(m));
            continue;
        }
        for (const Member& seen : members) {
            if (seen.stem && *seen.stem == *m.stem && seen.zst == m.zst)
                return std::unexpected(movie_refusal(CR::Archive, ERR_SITE()));
        }
        const std::optional<Use> use = rule_for(format, *m.stem);
        if (!m.zst && use == Use::Log) {
            if (log)
                return std::unexpected(movie_refusal(CR::Archive, ERR_SITE()));
            log = i;
        }
        members.push_back(std::move(m));
    }

    for (Member& m : members) {
        if (!m.stem || !m.zst)
            continue;
        const Use use = rule_for(format, *m.stem).value_or(Use::Presence);
        if (use == Use::Skip || use == Use::Presence)
            continue;
        const bool plain = std::ranges::any_of(members, [&](const Member& o) {
            return o.stem && !o.zst && *o.stem == *m.stem;
        });
        if (!plain)
            return std::unexpected(movie_refusal(CR::Archive, ERR_SITE()));
        m.stem.reset();
    }
    if (!log)
        return std::unexpected(movie_refusal(CR::NotAMovie, ERR_SITE()));
    const svc::ArchiveEntry& le = es[*log];
    if (le.size > MovieArchiveFormat::kLogMax)
        return std::unexpected(movie_refusal(CR::TooLong, ERR_SITE()));
    if (!readable(le))
        return std::unexpected(movie_refusal(CR::Archive, ERR_SITE()));

    std::string head;
    for (const Member& m : members) {
        if (!m.stem)
            continue;
        const Use use = m.zst ? Use::Presence : rule_for(format, *m.stem).value_or(format.others);
        const auto text = head_member(
            *archive,
            m.index,
            *m.stem,
            use,
            MovieArchiveFormat::kHeadMax - head.size()
        );
        if (!text)
            return std::unexpected(text.error());
        head.append(*text);
        if (head.size() > MovieArchiveFormat::kHeadMax)
            return std::unexpected(movie_refusal(CR::TooLong, ERR_SITE()));
    }

    auto f = archive->open(es[*log]);
    if (!f)
        return lower(f.error());
    return MovieText::open(std::move(archive), std::move(head), std::move(*f), le);
}

[[nodiscard]] Ex<std::unique_ptr<svc::IFile>> open_movie_archive(
    const svc::Vfs& vfs,
    std::string_view path,
    const MovieArchiveFormat& format
) {
    auto a = vfs.open_archive(path, MovieArchiveFormat::kMembersMax);
    if (!a) {
        const Error e = a.error();
        if (e.code != Errc::bad_format)
            return std::unexpected(e);
        const CR why = e.detail == static_cast<std::uint32_t>(E2BIG) ? CR::TooLong : CR::Archive;
        return std::unexpected(movie_refusal(why, e.site));
    }
    return open_movie_archive(std::move(*a), format);
}

}
