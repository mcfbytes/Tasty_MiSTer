// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/recorder_control.h"

#include <sys/stat.h>

#include <climits>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

#include "app/identity_latch.h"
#include "app/session_identity.h"
#include "infra/diag_log.h"

namespace mister::app {

namespace {

bool is_dir(const std::string& p) noexcept {
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

}

RecPath RecorderControl::resolve(
    std::string_view arg,
    std::string_view root,
    std::string_view name,
    std::time_t now
) noexcept {
    RecPath out{};
    if (arg.empty() || root.empty())
        return out;
    const std::string a(arg);
    std::string dir;
    std::string file;
    if (a.back() == '/' || is_dir(a)) {
        dir = a;
        std::tm tm{};
        (void)::localtime_r(&now, &tm);
        char stamp[32];
        (void)std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
        file = std::string(name.empty() ? "rec" : name) + "_" + stamp;
        file += RecordingName::kSidecarSuffix;
    } else {
        const auto slash = a.rfind('/');
        dir = slash == std::string::npos ? std::string(".") : a.substr(0, slash + 1);
        file = a.substr(slash == std::string::npos ? 0 : slash + 1);

        if (file.size() > RecordingName::kAviSuffix.size()
            && file.ends_with(RecordingName::kAviSuffix)) {
            file.resize(file.size() - RecordingName::kAviSuffix.size());
        }
        if (!RecordingName::names_sidecar(file))
            file += RecordingName::kSidecarSuffix;
    }
    char real[PATH_MAX];
    if (::realpath(dir.c_str(), real) == nullptr)
        return out;
    std::string r(real);
    if (r.back() != '/')
        r += '/';
    const std::string want(root.back() == '/' ? root : std::string(root) + "/");

    if (r.size() <= want.size() || r.compare(0, want.size(), want) != 0)
        return out;

    const std::optional<RecordingName> rec = RecordingName::from_sidecar(r + file);
    if (!rec || !rec->fits())
        return out;
    return rec->sidecar();
}

bool RecorderControl::publish_(
    RecOp op,
    std::string_view path,
    RecMode mode,
    RecOptions opt
) noexcept {
    TASTY_SEAT_BODY(RecorderControl);
    if (!requests_)
        return false;
    auto loan = requests_.acquire();
    if (!loan) {

        reap_();
        loan = requests_.acquire();
    }
    if (!loan) {
        if (w_.diag)
            w_.diag->appendf("{\"t\":\"rec\",\"k\":\"refused\",\"why\":\"queue\"}");
        return false;
    }
    gen_ = gen_.next_live();
    loan->gen = gen_;
    loan->op = op;
    loan->mode = mode;
    loan->opt = opt;
    loan->verdict = RecVerdict::None;
    (void)loan->path.assign(path);
    requests_.send(std::move(loan));
    return true;
}

std::string RecorderControl::name_() const {
    std::string name = "rec";
    if (w_.identity) {
        const std::optional<SessionIdentity> id = w_.identity->copy();
        if (id && !id->core.empty())
            name.assign(id->core.view());
    }
    return name;
}

bool RecorderControl::can_record(std::string_view path) const {
    return !resolve(path, w_.root, name_(), std::time(nullptr)).empty();
}

bool RecorderControl::request_(
    RecOp op,
    std::string_view path,
    RecMode mode,
    RecOptions opt
) noexcept {
    const RecPath p = resolve(path, w_.root, name_(), std::time(nullptr));
    if (p.empty()) {
        if (w_.diag)
            w_.diag->appendf("{\"t\":\"rec\",\"k\":\"refused\",\"why\":\"path\"}");
        return true;
    }
    (void)publish_(op, p.view(), mode, opt);
    return true;
}

bool RecorderControl::take_start(std::string_view path, RecMode mode, RecOptions opt) noexcept {
    TASTY_SEAT_BODY(RecorderControl);
    return request_(RecOp::Start, path, mode, opt);
}

bool RecorderControl::take_arm(std::string_view path, RecMode mode, RecOptions opt) noexcept {
    TASTY_SEAT_BODY(RecorderControl);
    return request_(RecOp::Arm, path, mode, opt);
}

bool RecorderControl::take_stop() noexcept {
    TASTY_SEAT_BODY(RecorderControl);
    (void)publish_(RecOp::Stop, {}, RecMode::Hash, {});
    return true;
}

bool RecorderControl::take_disarm() noexcept {
    TASTY_SEAT_BODY(RecorderControl);
    (void)publish_(RecOp::Disarm, {}, RecMode::Hash, {});
    return true;
}

void RecorderControl::reap_() noexcept {
    if (!requests_)
        return;
    RecorderStatus s{};
    if (w_.status)
        (void)w_.status->sample_into(s);
    for (;;) {
        auto loan = requests_.reap();
        if (!loan)
            return;
        last_verdict_ = loan->verdict;
        if (!w_.diag || loan->verdict == RecVerdict::None)
            continue;
        const char* remedy = rec_verdict_remedy(loan->verdict);
        w_.diag->appendf(
            "{\"t\":\"rec\",\"k\":\"verdict\",\"gen\":%s,\"v\":\"%s\",\"stride_mib\":%u,"
            "\"lowlat\":%u%s%s%s}",
            loan->gen.text().c_str(),
            rec_verdict_name(loan->verdict),
            static_cast<unsigned>(s.stride_mib),
            s.lowlat ? 1u : 0u,
            remedy != nullptr ? ",\"remedy\":\"" : "",
            remedy != nullptr ? remedy : "",
            remedy != nullptr ? "\"" : ""
        );
    }
}

void RecorderControl::tick() noexcept {
    TASTY_SEAT_BODY(RecorderControl);
    reap_();
    if (!w_.status || !w_.diag)
        return;
    RecorderStatus s{};
    if (!status_seen_.take_if_changed(*w_.status, s))
        return;
    if (s.end != RecVerdict::None && s.gen != logged_end_) {
        logged_end_ = s.gen;
        RecWriteStatus ws{};
        if (w_.writer)
            (void)w_.writer->sample_into(ws);
        w_.diag->appendf(
            "{\"t\":\"rec\",\"k\":\"end\",\"gen\":%s,\"why\":\"%s\",%s,\"werr\":%s}",
            s.gen.text().c_str(),
            rec_verdict_name(s.end),
            format_recorder_status(s).c_str(),
            ws.err.text().c_str()
        );
    }
}

}
