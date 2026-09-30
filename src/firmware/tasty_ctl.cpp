// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_ctl.h"

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <climits>
#include <ctime>
#include <linux/limits.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "app/path_text.h"
#include "cores/movie_codec.h"
#include "cores/movie_system.h"
#include "cores/rom_digest.h"
#include "infra/error.h"
#include "infra/persist.h"
#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::fw {
namespace {

const char* g_lock_path TASTY_PERSIST(proc, tasty_ctl_lock) = kTastyLockPath;
const char* g_pid_path TASTY_PERSIST(proc, tasty_ctl_pid) = kTastyPidPath;
const char* g_status_path TASTY_PERSIST(proc, tasty_ctl_status) = kTastyStatusPath;
const char* g_status_tmp_path TASTY_PERSIST(proc, tasty_ctl_status_tmp) = kTastyStatusTmpPath;
char g_status_tmp_buf[256] TASTY_PERSIST(proc, tasty_ctl_status_tmp_buf){};
const char* g_owner_comm TASTY_PERSIST(proc, tasty_ctl_comm) = "tasty";

Error os_err() noexcept { return Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)}; }

bool comm_matches(int pid, std::string_view want) noexcept {
    char path[64];
    std::snprintf(path, sizeof path, "/proc/%d/comm", pid);
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    char buf[32]{};
    const ssize_t n = ::read(fd, buf, sizeof buf - 1);
    ::close(fd);
    if (n <= 0) return false;
    std::string_view s{buf, static_cast<std::size_t>(n)};
    if (!s.empty() && s.back() == '\n') s.remove_suffix(1);
    return s == want;
}

bool comm_is_mister(int pid) noexcept { return comm_matches(pid, "MiSTer"); }

bool comm_is_owner(int pid) noexcept { return comm_matches(pid, g_owner_comm); }

std::string_view stem_of(std::string_view path) noexcept {
    const std::size_t slash = path.rfind('/');
    std::string_view base = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const std::size_t dot = base.rfind('.');
    if (dot != std::string_view::npos && dot > 0) base = base.substr(0, dot);
    return base;
}

bool is_dir(const char* p) noexcept {
    struct stat st {};
    return ::stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

}  // namespace

void tasty_set_ctl_paths(const char* lock, const char* pid, const char* status) noexcept {
    if (lock != nullptr) g_lock_path = lock;
    if (pid != nullptr) g_pid_path = pid;
    if (status != nullptr) {
        g_status_path = status;
        std::snprintf(g_status_tmp_buf, sizeof g_status_tmp_buf, "%s.tmp", status);
        g_status_tmp_path = g_status_tmp_buf;
    }
}

void tasty_set_owner_comm(const char* comm) noexcept {
    if (comm != nullptr && comm[0] != '\0') g_owner_comm = comm;
}

[[nodiscard]] Ex<int> tasty_lock_owner() noexcept {
    const int fd = ::open(g_lock_path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) return std::unexpected(os_err());
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        const int e = errno;
        ::close(fd);
        return std::unexpected(Error{Errc::busy, ERR_SITE(), static_cast<std::uint32_t>(e)});
    }
    return fd;
}

void tasty_clear_status() noexcept {
    (void)::unlink(g_status_path);
    (void)::unlink(g_status_tmp_path);
}

void tasty_unlock(int fd) noexcept {
    if (fd < 0) return;
    (void)::unlink(g_pid_path);
    tasty_clear_status();
    (void)::unlink(g_lock_path);
    (void)::flock(fd, LOCK_UN);
    ::close(fd);
}

ReturnHome::~ReturnHome() noexcept {
    if (lock_fd >= 0) tasty_unlock(lock_fd);
    if (!armed) return;
    if (spawned != nullptr) {
        *spawned += 1;
        return;
    }
    (void)tasty_spawn_stock();
}

[[nodiscard]] Ex<void> tasty_write_pid(int pid, std::string_view movie) noexcept {
    const int fd = ::open(g_pid_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return std::unexpected(os_err());
    char buf[app::kPathMax + 48];
    const int n = std::snprintf(buf, sizeof buf, "%d\n%.*s\n", pid, static_cast<int>(movie.size()),
                                movie.data());
    const ssize_t w = ::write(fd, buf, static_cast<std::size_t>(n));
    ::close(fd);
    if (w != n) return std::unexpected(os_err());
    return {};
}

std::optional<int> tasty_read_pid() noexcept {
    const int fd = ::open(g_pid_path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return std::nullopt;
    char buf[32]{};
    const ssize_t n = ::read(fd, buf, sizeof buf - 1);
    ::close(fd);
    if (n <= 0) return std::nullopt;
    int pid = 0;
    if (std::sscanf(buf, "%d", &pid) != 1 || pid <= 0) return std::nullopt;
    return pid;
}

std::optional<std::string> tasty_read_movie() noexcept {
    const int fd = ::open(g_pid_path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return std::nullopt;
    char buf[app::kPathMax + 48]{};
    const ssize_t n = ::read(fd, buf, sizeof buf - 1);
    ::close(fd);
    if (n <= 0) return std::nullopt;
    std::string_view s{buf, static_cast<std::size_t>(n)};
    const auto nl = s.find('\n');
    if (nl == std::string_view::npos || nl + 1 >= s.size()) return std::nullopt;
    s.remove_prefix(nl + 1);
    if (!s.empty() && s.back() == '\n') s.remove_suffix(1);
    if (s.empty()) return std::nullopt;
    return std::string(s);
}

std::string tasty_busy_text() noexcept {
    const auto pid = tasty_read_pid();
    const auto movie = tasty_read_movie();
    char buf[app::kPathMax + 64];
    if (pid && movie && !movie->empty()) {
        std::snprintf(buf, sizeof buf, "tasty is already playing %s (pid %d)", movie->c_str(),
                      *pid);
    } else if (pid) {
        std::snprintf(buf, sizeof buf, "tasty is already playing (pid %d)", *pid);
    } else {
        std::snprintf(buf, sizeof buf, "tasty is already playing");
    }
    return buf;
}

[[nodiscard]] Ex<void> tasty_stop_stock() noexcept {
    DIR* d = ::opendir("/proc");
    if (d == nullptr) return std::unexpected(os_err());
    int pids[16]{};
    unsigned n = 0;
    while (const dirent* e = ::readdir(d)) {
        if (e->d_name[0] < '1' || e->d_name[0] > '9') continue;
        const int pid = std::atoi(e->d_name);
        if (pid <= 0 || !comm_is_mister(pid)) continue;
        if (n < 16) pids[n++] = pid;
    }
    ::closedir(d);
    for (unsigned i = 0; i < n; ++i)
        (void)::kill(pids[i], SIGTERM);
    if (n != 0) ::sleep(2);
    for (unsigned i = 0; i < n; ++i) {
        if (::kill(pids[i], 0) == 0) (void)::kill(pids[i], SIGKILL);
    }
    const auto t0 = ::time(nullptr);
    for (unsigned i = 0; i < n; ++i) {
        while (::kill(pids[i], 0) == 0) {
            if (::time(nullptr) - t0 >= 3) break;
            ::usleep(50 * 1000);
        }
    }
    return {};
}

[[nodiscard]] Ex<void> tasty_write_cmd(std::string_view line) noexcept {
    const int fd = ::open(kMisterCmdPath, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return std::unexpected(os_err());
    char buf[1024];
    if (line.size() >= sizeof buf - 1) {
        ::close(fd);
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(line.size())});
    }
    std::memcpy(buf, line.data(), line.size());
    buf[line.size()] = '\n';
    const ssize_t w = ::write(fd, buf, line.size() + 1);
    ::close(fd);
    if (w < 0) return std::unexpected(os_err());
    return {};
}

[[nodiscard]] Ex<void> tasty_write_status(std::string_view json) noexcept {
    const int fd = ::open(g_status_tmp_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return std::unexpected(os_err());
    const ssize_t w = ::write(fd, json.data(), json.size());
    if (w < 0 || static_cast<std::size_t>(w) != json.size()) {
        const int e = errno;
        ::close(fd);
        (void)::unlink(g_status_tmp_path);
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(e)});
    }
    ::close(fd);
    if (::rename(g_status_tmp_path, g_status_path) != 0) return std::unexpected(os_err());
    return {};
}

[[nodiscard]] Ex<std::string> tasty_read_status() noexcept {
    const int fd = ::open(g_status_path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return std::unexpected(os_err());
    std::string s;
    s.resize(1024);
    const ssize_t n = ::read(fd, s.data(), s.size());
    ::close(fd);
    if (n < 0) return std::unexpected(os_err());
    s.resize(static_cast<std::size_t>(n));
    return s;
}

[[nodiscard]] Ex<void> tasty_signal_owner() noexcept {
    const auto pid = tasty_read_pid();
    if (!pid || !comm_is_owner(*pid)) return std::unexpected(Error{Errc::busy, ERR_SITE(), 0});
    if (::kill(*pid, SIGTERM) != 0) return std::unexpected(os_err());
    return {};
}

int tasty_spawn_stock() noexcept {
    const pid_t child = ::fork();
    if (child > 0) return 0;
    if (child < 0) return 1;
    (void)::setsid();
    const pid_t gc = ::fork();
    if (gc > 0) ::_exit(0);
    if (gc < 0) ::_exit(1);
    const int n = ::open("/dev/null", O_RDWR | O_CLOEXEC);
    if (n >= 0) {
        (void)::dup2(n, 0);
        (void)::dup2(n, 1);
        (void)::dup2(n, 2);
        if (n > 2) ::close(n);
    }
    char path[] = "/media/fat:/usr/bin:/bin";
    char pathk[] = "PATH=/media/fat:/usr/bin:/bin";
    (void)path;
    char* av[] = {const_cast<char*>(kStockMisterPath), const_cast<char*>(kMenuRbfName), nullptr};
    char* env[] = {pathk, nullptr};
    ::execve(kStockMisterPath, av, env);
    ::_exit(1);
}

TastyClientAct tasty_client_act(TastyVerb v) noexcept {
    const auto pid = tasty_read_pid();
    if (!pid || !comm_is_owner(*pid) || ::kill(*pid, 0) != 0) return TastyClientAct::Idle;
    if (v == TastyVerb::Stop) return TastyClientAct::Signaled;
    return TastyClientAct::WroteFifo;
}

bool tasty_rec_start_joins_owner() noexcept {
    return tasty_client_act(TastyVerb::RecStart) == TastyClientAct::WroteFifo;
}

int tasty_run_client(TastyVerb v) noexcept {
    const TastyClientAct act = tasty_client_act(v);
    if (act == TastyClientAct::Idle) {
        std::printf("idle\n");
        return 0;
    }
    if (v == TastyVerb::Status) {
        auto s = tasty_read_status();
        if (!s) {
            std::printf("idle\n");
            return 0;
        }
        std::fwrite(s->data(), 1, s->size(), stdout);
        return 0;
    }
    if (act == TastyClientAct::Signaled) {
        (void)tasty_signal_owner();
        return 0;
    }
    if (v == TastyVerb::RecStop) (void)tasty_write_cmd("rec_stop");
    return 0;
}

int tasty_preflight_rom(const svc::Vfs& vfs, std::string_view movie, std::string_view rom) {
    const auto sys = cores::movie_system_for(vfs, movie);
    if (!sys || sys->codec == nullptr) return 0;
    auto src = sys->codec->digest_source(vfs, rom);
    if (src) return 0;
    const auto ext = rom.size() >= 4 ? rom.substr(rom.size() - 4) : std::string_view{};
    if (ext == ".chd" || ext == ".CHD") {
        std::fprintf(stderr, "tasty: this disc is a .chd; hash a Redump .cue so play can start "
                             "before stock is stopped\n");
    } else {
        std::fprintf(stderr, "tasty: cannot hash rom %.*s\n", static_cast<int>(rom.size()),
                     rom.data());
    }
    return 1;
}

int tasty_check_movie(const svc::Vfs& vfs, std::string_view movie, std::string_view rom) {
    if (!vfs.file_exists(movie)) {
        std::fprintf(stderr, "tasty: missing movie %.*s\n", static_cast<int>(movie.size()),
                     movie.data());
        return 1;
    }
    const auto sys = cores::movie_system_for(vfs, movie);
    if (!sys || sys->codec == nullptr) {
        std::fprintf(stderr, "tasty: no codec for %.*s\n", static_cast<int>(movie.size()),
                     movie.data());
        return 1;
    }
    auto facts = cores::read_movie_facts(vfs, *sys->codec, movie);
    if (!facts) {
        std::fprintf(stderr, "tasty: cannot read movie %.*s\n", static_cast<int>(movie.size()),
                     movie.data());
        return 1;
    }
    if (!facts->has_digest) {
        std::printf("no checksum in this movie\n");
        return 0;
    }
    if (!vfs.file_exists(rom)) {
        std::fprintf(stderr, "tasty: missing rom %.*s\n", static_cast<int>(rom.size()), rom.data());
        return 1;
    }
    auto src = sys->codec->digest_source(vfs, rom);
    if (!src) {
        const auto ext = rom.size() >= 4 ? rom.substr(rom.size() - 4) : std::string_view{};
        if (ext == ".chd" || ext == ".CHD") {
            std::fprintf(stderr, "tasty: this disc is a .chd; hash a Redump .cue so play can start "
                                 "before stock is stopped\n");
        } else {
            std::fprintf(stderr, "tasty: cannot hash rom %.*s\n", static_cast<int>(rom.size()),
                         rom.data());
        }
        return 1;
    }
    auto f = vfs.open(src->file, svc::OpenMode::Read);
    if (!f) {
        std::fprintf(stderr, "tasty: cannot open rom %.*s\n", static_cast<int>(rom.size()),
                     rom.data());
        return 1;
    }
    cores::RomDigest dig{facts->digest.kind};
    if (!src->prefix.empty())
        dig.update(std::span<const std::uint8_t>(src->prefix.data(), src->prefix.size()));
    std::vector<std::uint8_t> buf(64 * 1024);
    std::uint64_t off = src->span.offset;
    std::uint64_t left = src->span.length;
    while (left != 0) {
        const std::size_t n = left < buf.size() ? static_cast<std::size_t>(left) : buf.size();
        const auto got = (*f)->read_at(off, std::as_writable_bytes(std::span(buf.data(), n)));
        if (!got) {
            std::fprintf(stderr, "tasty: rom read failed\n");
            return 1;
        }
        if (*got == 0) {
            std::fprintf(stderr, "tasty: short rom read\n");
            return 1;
        }
        dig.update(std::span<const std::uint8_t>(buf.data(), *got));
        off += *got;
        left -= *got;
    }
    const auto got = dig.finish();
    if (sys->codec->rom_matches(got, *facts)) {
        std::printf("rom matches\n");
        return 0;
    }
    std::printf("rom mismatch\n");
    return 3;
}

int tasty_info_movie(const svc::Vfs& vfs, std::string_view movie) {
    if (!vfs.file_exists(movie)) {
        std::fprintf(stderr, "tasty: missing movie %.*s\n", static_cast<int>(movie.size()),
                     movie.data());
        return 1;
    }
    const auto sys = cores::movie_system_for(vfs, movie);
    if (!sys || sys->codec == nullptr) {
        std::fprintf(stderr, "tasty: no codec for %.*s\n", static_cast<int>(movie.size()),
                     movie.data());
        return 1;
    }
    auto facts = cores::read_movie_facts(vfs, *sys->codec, movie);
    if (!facts) {
        std::fprintf(stderr, "tasty: cannot read movie %.*s\n", static_cast<int>(movie.size()),
                     movie.data());
        return 1;
    }
    std::printf("system %.*s\nplays %s\n", static_cast<int>(sys->conf_str_name.size()),
                sys->conf_str_name.data(), cores::movie_system_supported(*sys) ? "yes" : "no");
    return 0;
}

bool tasty_mkdir_p(std::string_view path) noexcept {
    if (path.empty()) return false;
    std::string s(path);
    while (!s.empty() && s.back() == '/')
        s.pop_back();
    if (s.empty()) return true;
    std::string cur;
    std::size_t i = 0;
    if (s[0] == '/') {
        cur = "/";
        i = 1;
    }
    while (i < s.size()) {
        const auto slash = s.find('/', i);
        const auto part = s.substr(i, slash == std::string::npos ? std::string::npos : slash - i);
        if (!part.empty()) {
            if (cur.size() > 1 || (!cur.empty() && cur[0] != '/')) cur += '/';
            cur += part;
            if (::mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) return false;
        }
        if (slash == std::string::npos) break;
        i = slash + 1;
    }
    return true;
}

bool tasty_resolve_args(TastyArgs& a) noexcept {
    auto one = [](app::PathText p) noexcept -> std::optional<app::PathText> {
        if (p.empty()) return p;
        if (p.view()[0] != '/') {
            char cwd[PATH_MAX];
            if (::getcwd(cwd, sizeof cwd) == nullptr) return std::nullopt;
            const std::string abs = std::string(cwd) + "/" + std::string(p.view());
            if (!p.assign(abs)) return std::nullopt;
        }
        char real[PATH_MAX];
        if (::realpath(p.c_str(), real) != nullptr) {
            if (!p.assign(real)) return std::nullopt;
        }
        return p;
    };
    const auto movie = one(a.movie);
    const auto rom = one(a.rom);
    const auto core = one(a.core);
    const auto record = one(a.record);
    if (!movie || !rom || !core || !record) return false;
    a.movie = *movie;
    a.rom = *rom;
    a.core = *core;
    a.record = *record;
    return true;
}

std::optional<app::PathText> tasty_prepare_record(app::PathText record,
                                                  std::string_view movie) noexcept {
    if (record.empty()) return record;
    std::string dir(record.view());
    const bool trailing = !dir.empty() && dir.back() == '/';
    if (trailing) dir.pop_back();
    const bool as_dir = trailing || is_dir(dir.c_str()) || ::access(dir.c_str(), F_OK) != 0;
    if (!as_dir) {
        const auto slash = dir.rfind('/');
        if (slash != std::string::npos && !tasty_mkdir_p(dir.substr(0, slash))) return std::nullopt;
        return record;
    }
    if (!tasty_mkdir_p(dir)) return std::nullopt;
    if (movie.empty()) {
        dir += '/';
        if (!record.assign(dir)) return std::nullopt;
        return record;
    }
    const std::string joined = dir + "/" + std::string(stem_of(movie));
    if (!record.assign(joined)) return std::nullopt;
    return record;
}

}  // namespace mister::fw
