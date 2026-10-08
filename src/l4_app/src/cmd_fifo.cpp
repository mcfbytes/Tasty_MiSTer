// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/cmd_fifo.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cctype>
#include <charconv>
#include <cerrno>
#include <cstring>
#include <optional>
#include <system_error>
#include <utility>

#include "app/cmd_verb.h"
#include "app/path_text.h"
#include "app/xml_kind.h"
#include "proto/volume_cmd.h"

namespace mister::app {
namespace {

bool starts_with(std::string_view s, std::string_view p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

bool iequal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) return false;
    }
    return true;
}

XmlKind xml_kind_of(std::string_view s) {
    if (s.size() <= 4) return XmlKind::Rbf;
    const std::string_view tail = s.substr(s.size() - 4);
    if (iequal(tail, ".mra")) return XmlKind::Mra;
    if (iequal(tail, ".mgl")) return XmlKind::Mgl;
    return XmlKind::Rbf;
}

bool is_blank(char c) { return c == ' ' || c == '\t'; }

std::string_view lstrip(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size() && is_blank(s[i]))
        ++i;
    return s.substr(i);
}

std::optional<std::string_view> next_token(std::string_view& rest) {
    rest = lstrip(rest);
    if (rest.empty()) return std::nullopt;
    if (rest.front() == '"') {
        const std::size_t close = rest.find('"', 1);
        if (close == std::string_view::npos || close == 1) return std::nullopt;
        const std::string_view tok = rest.substr(1, close - 1);
        rest.remove_prefix(close + 1);
        if (!rest.empty() && !is_blank(rest.front())) return std::nullopt;
        return tok;
    }
    std::size_t n = 0;
    while (n < rest.size() && !is_blank(rest[n]))
        ++n;
    const std::string_view tok = rest.substr(0, n);
    rest.remove_prefix(n);
    return tok;
}

std::optional<std::int32_t> small_int(std::string_view s) {
    if (s.size() > 1 && s.front() == '+' && s[1] != '-') s.remove_prefix(1);
    const std::size_t digits = !s.empty() && s.front() == '-' ? s.size() - 1 : s.size();
    if (digits > 9) return std::nullopt;
    std::int32_t v = 0;
    const char* const end = s.data() + s.size();
    const auto [at, ec] = std::from_chars(s.data(), end, v);
    if (ec != std::errc{} || at != end) return std::nullopt;
    return v;
}

std::optional<CmdVerb::TasPlay> parse_tas_play(std::string_view args) {
    CmdVerb::TasPlay v{};
    const auto movie = next_token(args);
    const auto rom = next_token(args);
    if (!movie || !rom) return std::nullopt;
    v.movie = *movie;
    v.rom = *rom;
    if (const auto phase = next_token(args)) {
        const auto p = small_int(*phase);
        if (!p || *p < 0) return std::nullopt;
        v.phase_us = static_cast<std::uint32_t>(*p);
        if (const auto lead = next_token(args)) {
            const auto l = small_int(*lead);
            if (!l) return std::nullopt;
            v.lead = *l;
        }
    }
    if (!lstrip(args).empty()) return std::nullopt;
    return v;
}

struct ParsedRec {
    std::string_view path;
    RecMode mode = RecMode::Avi;
    RecOptions opt{};
};

std::optional<ParsedRec> parse_rec(std::string_view args) {
    const auto path = next_token(args);
    if (!path || path->empty()) return std::nullopt;
    ParsedRec v;
    v.path = *path;
    bool saw_mode = false;
    unsigned seen = 0;
    constexpr unsigned kCodec = 1u, kScale = 2u, kEvery = 4u, kFrom = 8u, kTo = 16u, kSeg = 32u,
                       kMotion = 64u;
    while (const auto tok = next_token(args)) {
        const auto eq = tok->find('=');
        if (eq == std::string_view::npos) {
            if (saw_mode) return std::nullopt;
            if (*tok == "hash")
                v.mode = RecMode::Hash;
            else if (*tok != "avi")
                return std::nullopt;
            saw_mode = true;
            continue;
        }
        const std::string_view key = tok->substr(0, eq);
        const std::string_view val = tok->substr(eq + 1);
        if (val.empty()) return std::nullopt;
        const auto take = [&](unsigned bit) {
            if ((seen & bit) != 0) return false;
            seen |= bit;
            return true;
        };
        if (key == "codec") {
            const auto c = parse_rec_codec(val);
            if (!c || !take(kCodec)) return std::nullopt;
            v.opt.codec = *c;
        } else if (key == "scale") {
            const auto s = parse_rec_scale(val);
            if (!s || !take(kScale)) return std::nullopt;
            v.opt.scale = *s;
        } else if (key == "motion") {
            const auto m = parse_rec_motion(val);
            if (!m || !take(kMotion)) return std::nullopt;
            v.opt.motion = *m;
        } else if (key == "every") {
            const auto n = parse_rec_every(val);
            if (!n || !take(kEvery)) return std::nullopt;
            v.opt.every = *n;
        } else if (key == "from" || key == "to") {
            std::int32_t n = 0;
            const auto [at, ec] = std::from_chars(val.data(), val.data() + val.size(), n);
            if (ec != std::errc{} || at != val.data() + val.size()) return std::nullopt;
            const unsigned bit = key == "from" ? kFrom : kTo;
            if (!take(bit)) return std::nullopt;
            if (key == "from")
                v.opt.from_frame = n;
            else
                v.opt.to_frame = n;
        } else if (key == "segment") {
            const auto n = parse_rec_size(val);
            if (!n || !rec_segment_ok(*n) || !take(kSeg)) return std::nullopt;
            v.opt.segment_bytes = *n;
        } else {
            return std::nullopt;
        }
    }
    if (!lstrip(args).empty() || !rec_bounds_ok(v.opt)) return std::nullopt;
    return v;
}

CmdLineOutcome taken(bool t) noexcept {
    return t ? CmdLineOutcome::Routed : CmdLineOutcome::Unrouted;
}

}  // namespace

static_assert(kCmdLineMax <= app::kPathMax, "a FIFO read's operand always fits PathText");

CmdLineOutcome deliver_cmd_line(std::string_view line, ICmdVerbSink& sink) noexcept {

    if (starts_with(line, "fb_cmd")) return taken(sink.on(CmdVerb::FbCmd{line}));

    if (starts_with(line, "video_mode "))
        return taken(sink.on(CmdVerb::VideoMode{line.substr(11)}));

    if (starts_with(line, "load_core ")) {
        const std::string_view path = line.substr(10);
        switch (xml_kind_of(line)) {
            case XmlKind::Mgl:
                return taken(sink.on(CmdVerb::Playlist{path}));
            case XmlKind::Mra:
                return taken(sink.on(CmdVerb::LoadCore{path, CmdVerb::RbfOrMra::Mra}));
            case XmlKind::Rbf:
                return taken(sink.on(CmdVerb::LoadCore{path, CmdVerb::RbfOrMra::Rbf}));
        }
        return CmdLineOutcome::Unrecognised;
    }

    if (starts_with(line, "screenshot")) {
        std::string_view rest = lstrip(line.substr(10));
        bool scaled = false;
        if (starts_with(rest, "scaled") && (rest.size() == 6 || is_blank(rest[6]))) {
            scaled = true;
            rest = lstrip(rest.substr(6));
        }
        return taken(sink.on(CmdVerb::Screenshot{rest, scaled}));
    }

    if (starts_with(line, "volume ")) {
        const std::string_view arg = line.substr(7);
        if (arg == "mute") return taken(sink.on(CmdVerb::Volume{proto::VolumeCmd::SetMute, 1}));
        if (arg == "unmute") return taken(sink.on(CmdVerb::Volume{proto::VolumeCmd::SetMute, 0}));
        if (arg.size() == 1 && arg[0] >= '0' && arg[0] <= '7') {

            const auto n = static_cast<std::int8_t>(arg[0] - '0');
            return taken(sink.on(
                CmdVerb::Volume{proto::VolumeCmd::SetAtten, static_cast<std::int8_t>(7 - n)}));
        }
        return CmdLineOutcome::Unrecognised;
    }

    if (line == "rtstats") return taken(sink.on(CmdVerb::RtStats{}));

    if (starts_with(line, "tas_play ")) {
        const auto v = parse_tas_play(line.substr(9));
        if (!v) return CmdLineOutcome::Unrecognised;
        return taken(sink.on(*v));
    }
    if (line == "tas_stop") return taken(sink.on(CmdVerb::TasStop{}));

    if (starts_with(line, "rec_start ") || starts_with(line, "rec_arm ")) {
        const bool arm = starts_with(line, "rec_arm ");
        const auto v = parse_rec(line.substr(arm ? 8 : 10));
        if (!v) return CmdLineOutcome::Unrecognised;
        if (arm) return taken(sink.on(CmdVerb::RecArm{v->path, v->mode, v->opt}));
        return taken(sink.on(CmdVerb::RecStart{v->path, v->mode, v->opt}));
    }
    if (line == "rec_stop") return taken(sink.on(CmdVerb::RecStop{}));
    if (line == "rec_disarm") return taken(sink.on(CmdVerb::RecDisarm{}));

    return CmdLineOutcome::Unrecognised;
}

CmdFifo::CmdFifo(UniqueFd fd, std::string_view path, ICmdVerbSink& route) noexcept
    : fd_(static_cast<UniqueFd&&>(fd)), route_(route) {

    (void)path_.assign(path);
}

CmdFifo::CmdFifo(CmdFifo&& o) noexcept
    : fd_(static_cast<UniqueFd&&>(o.fd_)), path_(o.path_), last_bad_(o.last_bad_), stats_(o.stats_),
      route_(o.route_), routed_(o.routed_) {

    o.path_.clear();
    o.last_bad_.clear();
}

CmdFifo& CmdFifo::operator=(CmdFifo&& o) noexcept {
    if (this != &o) {
        if (fd_.valid() && !path_.empty()) (void)::unlink(path_.c_str());
        fd_ = static_cast<UniqueFd&&>(o.fd_);
        path_ = o.path_;
        last_bad_ = o.last_bad_;
        stats_ = o.stats_;
        route_ = o.route_;
        routed_ = o.routed_;
        o.path_.clear();
        o.last_bad_.clear();
    }
    return *this;
}

CmdFifo::~CmdFifo() {

    if (fd_.valid() && !path_.empty()) (void)::unlink(path_.c_str());
}

std::string_view CmdFifo::path() const noexcept { return path_.view(); }

std::string_view CmdFifo::last_unrecognised() const noexcept { return last_bad_.view(); }

Ex<CmdFifo> CmdFifo::open(const char* path, ICmdVerbSink& route) {
    if (path == nullptr) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    const std::string_view p{path};
    if (p.empty() || p.size() >= kPathMax) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(p.size())});
    }

    if (::unlink(path) != 0 && errno != ENOENT) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }

    if (::mkfifo(path, 0666) != 0) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }

    const int fd = ::open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        const auto e = static_cast<std::uint32_t>(errno);
        (void)::unlink(path);
        return std::unexpected(Error{Errc::os, ERR_SITE(), e});
    }
    return CmdFifo{UniqueFd{fd}, p, route};
}

Ex<unsigned> CmdFifo::service() {

    struct PublishOnExit {
        CmdFifo* self;
        ~PublishOnExit() { self->cell_.publish(self->stats_); }
    } publish_on_exit{this};

    if (!fd_.valid()) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), EBADF});
    }

    char buf[kCmdLineMax];
    const ssize_t len = ::read(fd_.get(), buf, sizeof(buf) - 1);

    if (len < 0) {
        const int e = errno;

        if (e == EAGAIN || e == EWOULDBLOCK || e == EINTR) return 0u;
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(e)});
    }
    if (len == 0) return 0u;
    ++stats_.reads;

    std::string_view rest{buf, static_cast<std::size_t>(len)};
    unsigned routed = 0;

    while (!rest.empty()) {
        const std::size_t nl = rest.find('\n');
        const std::string_view line = (nl == std::string_view::npos) ? rest : rest.substr(0, nl);
        rest = (nl == std::string_view::npos) ? std::string_view{} : rest.substr(nl + 1);

        if (line.empty()) continue;
        ++stats_.lines;

        switch (deliver_cmd_line(line, route_.get())) {
            case CmdLineOutcome::Routed:
                ++routed_;
                ++routed;
                break;
            case CmdLineOutcome::Unrouted:
                ++stats_.unrouted;
                break;
            case CmdLineOutcome::Unrecognised:

                ++stats_.unrecognised;
                (void)last_bad_.assign(line);
                break;
        }
    }
    return routed;
}

}  // namespace mister::app
