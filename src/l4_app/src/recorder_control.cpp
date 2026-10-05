// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/recorder_control.h"

#include <sys/stat.h>

#include <climits>
#include <cstdlib>
#include <memory>
#include <string>

#include "app/identity_latch.h"
#include "app/session_identity.h"
#include "infra/diag_log.h"

namespace mister::app {

namespace {

bool is_dir(const std::string& p) noexcept {
    struct stat st {};
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool ends_with(std::string_view s, std::string_view t) noexcept {
    return s.size() >= t.size() && s.substr(s.size() - t.size()) == t;
}

}  // namespace

RecPath RecorderControl::resolve(std::string_view arg, std::string_view root, std::string_view name,
                                 std::time_t now) noexcept {
    RecPath out{};
    if (arg.empty() || root.empty()) return out;
    const std::string a(arg);
    std::string dir;
    std::string file;
    if (a.back() == '/' || is_dir(a)) {
        dir = a;
        std::tm tm{};
        (void)::localtime_r(&now, &tm);
        char stamp[32];
        (void)std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
        file = std::string(name.empty() ? "rec" : name) + "_" + stamp + ".frames.tsv";
    } else {
        const auto slash = a.rfind('/');
        dir = slash == std::string::npos ? std::string(".") : a.substr(0, slash + 1);
        file = a.substr(slash == std::string::npos ? 0 : slash + 1);

        if (ends_with(file, ".avi") && file.size() > 4) file.resize(file.size() - 4);
        if (!ends_with(file, ".tsv")) file += ".frames.tsv";
    }
    char real[PATH_MAX];
    if (::realpath(dir.c_str(), real) == nullptr) return out;
    std::string r(real);
    if (r.back() != '/') r += '/';
    const std::string want(root.back() == '/' ? root : std::string(root) + "/");

    if (r.size() <= want.size() || r.compare(0, want.size(), want) != 0) return out;

    std::string base = r + file;
    base.resize(base.size() - (ends_with(base, ".frames.tsv") ? 11u : 4u));
    if (RecPath seg{}; !seg.assign(base + "_000.avi")) return out;
    (void)out.assign(r + file);
    return out;
}

bool RecorderControl::publish_(RecOp op, std::string_view path, RecMode mode,
                               RecOptions opt) noexcept {
    TASTY_SEAT_BODY(RecorderControl);
    if (w_.control == nullptr) return false;
    RecControl c{};
    c.gen = ++gen_;
    if (gen_ == 0) c.gen = gen_ = 1;
    c.op = op;
    c.mode = mode;
    c.opt = opt;
    (void)c.path.assign(path);
    w_.control->publish(c);
    if (w_.capture_wake != nullptr) w_.capture_wake->kick();
    return true;
}

std::string RecorderControl::name_() const {
    std::string name = "rec";
    if (w_.identity != nullptr) {

        auto id = std::make_unique<SessionIdentity>();
        if (w_.identity->copy(*id) && id->core[0] != '\0') name = id->core;
    }
    return name;
}

bool RecorderControl::can_record(std::string_view path) const {
    return !resolve(path, w_.root, name_(), std::time(nullptr)).empty();
}

bool RecorderControl::request_(RecOp op, std::string_view path, RecMode mode,
                               RecOptions opt) noexcept {
    const RecPath p = resolve(path, w_.root, name_(), std::time(nullptr));
    if (p.empty()) {
        if (w_.diag != nullptr)
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

void RecorderControl::tick() noexcept {
    TASTY_SEAT_BODY(RecorderControl);
    if (w_.status == nullptr) return;
    RecorderStatus s{};
    if (!status_seen_.take_if_changed(*w_.status, s)) return;
    if (w_.diag == nullptr) return;
    if (s.verdict != RecVerdict::None && s.answered != logged_answer_) {
        logged_answer_ = s.answered;
        const char* remedy = rec_verdict_remedy(s.verdict);
        w_.diag->appendf(
            "{\"t\":\"rec\",\"k\":\"verdict\",\"gen\":%u,\"v\":\"%s\",\"stride_mib\":%u,"
            "\"lowlat\":%u%s%s%s}",
            static_cast<unsigned>(s.answered), rec_verdict_name(s.verdict),
            static_cast<unsigned>(s.stride_mib), static_cast<unsigned>(s.lowlat),
            remedy != nullptr ? ",\"remedy\":\"" : "", remedy != nullptr ? remedy : "",
            remedy != nullptr ? "\"" : "");
    }
    if (s.end != RecVerdict::None && s.gen != logged_end_) {
        logged_end_ = s.gen;
        RecWriteStatus ws{};
        if (w_.writer != nullptr) (void)w_.writer->sample_into(ws);
        w_.diag->appendf("{\"t\":\"rec\",\"k\":\"end\",\"gen\":%u,\"why\":\"%s\",%s,\"werr\":%d}",
                         static_cast<unsigned>(s.gen), rec_verdict_name(s.end),
                         format_recorder_status(s).c_str(), static_cast<int>(ws.err));
    }
}

}  // namespace mister::app
