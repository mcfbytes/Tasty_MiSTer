// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_ctl.h"

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <climits>
#include <ctime>
#include <linux/limits.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "app/path_text.h"
#include "app/replay_feeder.h"
#include "tasty_hub.h"
#include "cores/movie_codec.h"
#include "cores/movie_system.h"
#include "cores/rom_digest.h"
#include "infra/error.h"
#include "infra/persist.h"
#include "os/writeback_probe.h"
#include "svc/file.h"
#include "svc/scan_filter.h"
#include "svc/vfs.h"

namespace mister::fw {
namespace {

void report_unhashable(const cores::IMovieCodec& codec, std::string_view rom,
                       const Error& e) noexcept {
    using CR = cores::IMovieCodec::Refusal;
    const int n = static_cast<int>(rom.size());
    const auto why = static_cast<CR>(e.detail);
    const std::string_view images = codec.disc_images();
    if (e.code != Errc::bad_format)
        std::fprintf(stderr, "tasty: cannot read rom %.*s\n", n, rom.data());
    else if (why == CR::ChdUnsupported)
        std::fprintf(stderr, "tasty: this build of tasty cannot read .chd disc images: %.*s\n", n,
                     rom.data());
    else if (why == CR::Disk && !images.empty())
        std::fprintf(stderr, "tasty: cannot hash disc image %.*s; use %.*s\n", n, rom.data(),
                     static_cast<int>(images.size()), images.data());
    else
        std::fprintf(stderr, "tasty: cannot hash rom %.*s\n", n, rom.data());
}

const char* g_lock_path TASTY_PERSIST(proc, tasty_ctl_lock) = kTastyLockPath;
const char* g_pid_path TASTY_PERSIST(proc, tasty_ctl_pid) = kTastyPidPath;
const char* g_status_path TASTY_PERSIST(proc, tasty_ctl_status) = kTastyStatusPath;
const char* g_status_tmp_path TASTY_PERSIST(proc, tasty_ctl_status_tmp) = kTastyStatusTmpPath;
char g_status_tmp_buf[256] TASTY_PERSIST(proc, tasty_ctl_status_tmp_buf){};
const char* g_owner_comm TASTY_PERSIST(proc, tasty_ctl_comm) = "tasty";
std::atomic<int> g_home_claimed TASTY_PERSIST(proc, tasty_home_claimed){0};
std::atomic<ReturnHome*> g_return_home TASTY_PERSIST(proc, tasty_return_home){nullptr};
static_assert(std::atomic<int>::is_always_lock_free);
static_assert(std::atomic<ReturnHome*>::is_always_lock_free);

char g_home_exe[PATH_MAX] TASTY_PERSIST(proc, tasty_home_exe) = "/media/fat/MiSTer";
char g_home_menu[PATH_MAX] TASTY_PERSIST(proc, tasty_home_menu) = "menu.rbf";
char g_home_env[] TASTY_PERSIST(proc, tasty_home_env) = "PATH=/media/fat:/usr/bin:/bin";

void write_stderr(const char* p, std::size_t n) noexcept {
    if (p == nullptr || n == 0) return;
    pollfd pf{};
    pf.fd = STDERR_FILENO;
    pf.events = POLLOUT;
    if (::poll(&pf, 1, 0) <= 0) return;
    if ((pf.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) return;
    const ssize_t w = ::write(STDERR_FILENO, p, n);
    (void)w;
}

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

std::optional<timespec> mono_deadline(unsigned seconds) noexcept {
    timespec out{};
    if (::clock_gettime(CLOCK_MONOTONIC, &out) != 0) return std::nullopt;
    out.tv_sec += static_cast<time_t>(seconds);
    return out;
}

void sleep_until(const timespec& deadline) noexcept {
    while (::clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, nullptr) == EINTR) {
    }
}

void sleep_remaining(unsigned seconds) noexcept {
    timespec left{};
    left.tv_sec = static_cast<time_t>(seconds);
    while (::nanosleep(&left, &left) != 0 && errno == EINTR) {
    }
}

bool deadline_passed(const timespec& deadline) noexcept {
    timespec now{};
    if (::clock_gettime(CLOCK_MONOTONIC, &now) != 0) return true;
    if (now.tv_sec != deadline.tv_sec) return now.tv_sec > deadline.tv_sec;
    return now.tv_nsec >= deadline.tv_nsec;
}

void sleep_poll_slice(const timespec& deadline) noexcept {
    timespec now{};
    if (::clock_gettime(CLOCK_MONOTONIC, &now) != 0) return;
    timespec slice = now;
    constexpr long kPollNs = 50L * 1000L * 1000L;
    constexpr long kSecNs = 1000L * 1000L * 1000L;
    slice.tv_nsec += kPollNs;
    if (slice.tv_nsec >= kSecNs) {
        slice.tv_sec += 1;
        slice.tv_nsec -= kSecNs;
    }
    if (slice.tv_sec > deadline.tv_sec ||
        (slice.tv_sec == deadline.tv_sec && slice.tv_nsec > deadline.tv_nsec))
        slice = deadline;
    sleep_until(slice);
}

void sleep_grace(unsigned seconds) noexcept {
    const std::optional<timespec> deadline = mono_deadline(seconds);
    if (!deadline) {
        sleep_remaining(seconds);
        return;
    }
    sleep_until(*deadline);
}

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

bool ends_with_avi(std::string_view s) noexcept {
    if (s.size() < 4) return false;
    const auto e = s.substr(s.size() - 4);
    auto low = [](char c) noexcept {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    };
    return low(e[0]) == '.' && low(e[1]) == 'a' && low(e[2]) == 'v' && low(e[3]) == 'i';
}

bool is_file(const char* p) noexcept {
    struct stat st {};
    return ::stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

std::string rooted(const svc::Vfs& vfs, std::string_view rel) {
    std::string p = vfs.root_path();
    while (p.size() > 1 && p.back() == '/')
        p.pop_back();
    if (p.empty()) p = "/";
    if (p.back() != '/') p.push_back('/');
    p.append(rel);
    return p;
}

constexpr unsigned kRomNameCap = 4000;
constexpr std::uint64_t kRomHashBytes = 64ull << 20;

enum class DigestFail : std::uint8_t { None, Open, Read, Short };
struct Digested {
    std::optional<cores::DigestValue> value{};
    std::optional<cores::DigestValue> alt{};
    DigestFail fail = DigestFail::None;
};

Digested digest_of(const svc::Vfs& vfs, const cores::IMovieCodec::DigestSource& src,
                   cores::DigestKind kind) {
    auto f = vfs.open(src.file, svc::OpenMode::Read);
    if (!f) return {.fail = DigestFail::Open};
    cores::RomDigest dig{kind};
    cores::RomDigest alt{kind};
    if (!src.prefix.empty())
        dig.update(std::span<const std::uint8_t>(src.prefix.data(), src.prefix.size()));
    if (src.alt_prefix)
        alt.update(std::span<const std::uint8_t>(src.alt_prefix->data(), src.alt_prefix->size()));
    std::vector<std::uint8_t> buf(64 * 1024);
    std::uint64_t off = src.span.offset;
    std::uint64_t left = src.span.length;
    while (left != 0) {
        const std::size_t n = left < buf.size() ? static_cast<std::size_t>(left) : buf.size();
        const auto got = (*f)->read_at(off, std::as_writable_bytes(std::span(buf.data(), n)));
        if (!got || *got == 0) return {.fail = got ? DigestFail::Short : DigestFail::Read};
        dig.update(std::span<const std::uint8_t>(buf.data(), *got));
        if (src.alt_prefix) alt.update(std::span<const std::uint8_t>(buf.data(), *got));
        off += *got;
        left -= *got;
    }
    if (!src.alt_prefix) return {.value = dig.finish()};
    return {.value = dig.finish(), .alt = alt.finish()};
}

bool digest_matches(const cores::IMovieCodec& codec, const Digested& d,
                    const cores::IMovieCodec::Facts& facts) {
    if (d.value && codec.rom_matches(*d.value, facts)) return true;
    return d.alt && codec.rom_matches(*d.alt, facts);
}

int lookup_rom(const svc::Vfs& vfs, const cores::IMovieCodec& codec,
               const cores::IMovieCodec::Facts& facts, std::string_view folder,
               app::PathText& rom) {
    if (!facts.has_digest) {
        std::fprintf(stderr, "tasty: this movie names no ROM checksum; pass --rom\n");
        return 1;
    }
    const std::string rel = std::string("media/fat/games/") + std::string(folder);
    const svc::ScanFilter filt{};
    unsigned seen = 0;
    std::uint64_t hashed = 0;

    bool stopped = false;

    auto consider = [&](const std::string& child) -> int {
        ++seen;
        auto src = codec.digest_source(vfs, child);
        if (!src) return 0;
        const std::uint64_t prefix_n =
            src->prefix.size() + (src->alt_prefix ? src->alt_prefix->size() : 0);
        const std::uint64_t span_n = src->span.length;

        if (prefix_n > kRomHashBytes - hashed || span_n > kRomHashBytes - hashed - prefix_n) {
            stopped = true;
            return 0;
        }
        hashed += prefix_n + span_n;
        const auto d = digest_of(vfs, *src, facts.digest.kind);
        if (!digest_matches(codec, d, facts)) return 0;
        return rom.assign(child) ? 1 : -1;
    };
    std::vector<std::string> subdirs;

    auto walk = [&](const std::string& dir, bool top) -> int {
        auto listing = vfs.scan(dir, filt);
        if (!listing) return 0;
        for (const svc::DirEntry& e : *listing) {
            if (e.name == "." || e.name == "..") continue;
            if (e.is_dir) {
                if (top) subdirs.push_back(dir + "/" + e.name);
                continue;
            }
            if (seen >= kRomNameCap) {
                stopped = true;
                return 0;
            }
            const int r = consider(rooted(vfs, dir + "/" + e.name));
            if (r != 0 || stopped) return r;
        }
        return 0;
    };
    int r = walk(rel, true);
    for (std::size_t i = 0; r == 0 && !stopped && i < subdirs.size(); ++i)
        r = walk(subdirs[i], false);
    if (r == 1) return 0;
    if (r < 0) {
        std::fprintf(stderr, "tasty: the matching ROM path is too long; pass --rom\n");
        return 1;
    }
    if (stopped)
        std::fprintf(stderr,
                     "tasty: no ROM among the first %u files in /media/fat/games/%.*s matches this "
                     "movie (the search stops there); pass --rom\n",
                     seen, static_cast<int>(folder.size()), folder.data());
    else
        std::fprintf(stderr,
                     "tasty: no ROM in /media/fat/games/%.*s matches this movie; pass --rom\n",
                     static_cast<int>(folder.size()), folder.data());
    return 1;
}

struct LogCount {
    std::uint32_t frames = 0;
    bool capped = false;
};

LogCount count_log_frames(const svc::Vfs& vfs, const cores::IMovieCodec& codec,
                          std::string_view movie, const cores::IMovieCodec::Facts& facts) {
    bool capped = false;
    auto f = codec.open_movie(vfs, movie);
    if (!f) return {};
    std::vector<char> buf(4096);
    std::string line;
    std::uint64_t off = 0;
    std::uint32_t n = 0;
    bool in = false;
    bool stop = false;
    constexpr std::uint32_t kCap = 1u << 20;
    auto take = [&](std::string_view row) {
        if (stop) return;
        if (!row.empty() && row.back() == '\r') row.remove_suffix(1);
        if (!in) {
            if (!codec.starts_log(row)) return;
            in = true;
        }
        if (codec.ends_log(row)) {
            stop = true;
            return;
        }
        if (!codec.frame(row, facts)) return;
        if (n >= kCap) {
            capped = true;
            stop = true;
            return;
        }
        ++n;
    };
    for (;;) {
        const auto got =
            (*f)->read_at(off, std::as_writable_bytes(std::span(buf.data(), buf.size())));
        if (!got || *got == 0) break;
        off += *got;
        for (std::size_t i = 0; i < *got && !stop; ++i) {
            const char ch = buf[i];
            if (ch == '\n') {
                take(line);
                line.clear();
            } else if (line.size() < cores::IMovieCodec::kLineMax) {
                line.push_back(ch);
            }
        }
        if (stop || *got < buf.size()) break;
    }
    if (!stop && !line.empty()) take(line);
    return {.frames = n, .capped = capped};
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

void tasty_set_home_image(const char* exe, const char* arg) noexcept {
    if (exe != nullptr && exe[0] != '\0') std::snprintf(g_home_exe, sizeof g_home_exe, "%s", exe);
    if (arg != nullptr && arg[0] != '\0') std::snprintf(g_home_menu, sizeof g_home_menu, "%s", arg);
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

ReturnHome::ReturnHome() noexcept {

    g_home_claimed.store(0, std::memory_order_seq_cst);
    g_return_home.store(this, std::memory_order_seq_cst);
}

ReturnHome::~ReturnHome() noexcept {
    if (lock_fd >= 0) tasty_unlock(lock_fd);
    if (armed) (void)tasty_launch_home();
    ReturnHome* self = this;
    (void)g_return_home.compare_exchange_strong(self, nullptr, std::memory_order_seq_cst);
}

void tasty_say(std::string_view line) noexcept {
    char buf[1600];
    std::size_t n = line.size();
    if (n >= sizeof buf) n = sizeof buf - 1;
    if (n != 0) std::memcpy(buf, line.data(), n);
    if (n == 0 || buf[n - 1] != '\n') buf[n++] = '\n';
    write_stderr(buf, n);
}

int tasty_launch_home() noexcept {
    if (!tasty_home_may_launch()) return 0;
    int expected = 0;
    if (!g_home_claimed.compare_exchange_strong(expected, 1, std::memory_order_seq_cst)) return 0;
    ReturnHome* h = g_return_home.load(std::memory_order_seq_cst);
    if (h != nullptr && h->spawned != nullptr) {
        *h->spawned += 1;
        return 0;
    }
    return tasty_spawn_stock();
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

[[nodiscard]] Ex<bool> tasty_stop_stock() noexcept {
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
    if (n != 0) sleep_grace(2);
    for (unsigned i = 0; i < n; ++i) {
        if (::kill(pids[i], 0) == 0) (void)::kill(pids[i], SIGKILL);
    }
    if (const std::optional<timespec> deadline = mono_deadline(3)) {
        for (unsigned i = 0; i < n; ++i) {
            while (::kill(pids[i], 0) == 0 && !deadline_passed(*deadline))
                sleep_poll_slice(*deadline);
        }
    } else {
        sleep_remaining(3);
    }
    for (unsigned i = 0; i < n; ++i) {
        if (::kill(pids[i], 0) == 0) return false;
        if (errno != ESRCH) return false;
    }
    return true;
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

    struct sigaction dfl {};
    dfl.sa_handler = SIG_DFL;
    ::sigemptyset(&dfl.sa_mask);
    const int owned[] = {SIGHUP, SIGPIPE, SIGINT, SIGTERM, SIGQUIT, SIGSEGV, SIGABRT, SIGBUS};
    for (const int sig : owned)
        (void)::sigaction(sig, &dfl, nullptr);
    sigset_t empty;
    ::sigemptyset(&empty);
    (void)::sigprocmask(SIG_SETMASK, &empty, nullptr);
    char* av[] = {g_home_exe, g_home_menu, nullptr};
    char* env[] = {g_home_env, nullptr};
    ::execve(g_home_exe, av, env);
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

const char* tasty_movie_refusal_token(std::uint32_t codec_refusal) noexcept {
    using R = cores::IMovieCodec::Refusal;
    if (codec_refusal >= static_cast<std::uint32_t>(R::kCount)) return "movie";
    switch (static_cast<R>(codec_refusal)) {
        case R::NotAMovie:
            return "not_a_movie";
        case R::Savestate:
            return "savestate";
        case R::Binary:
            return "binary";
        case R::Disk:
            return "disk";
        case R::PortType:
            return "port_type";
        case R::Multitap:
            return "multitap";
        case R::Checksum:
            return "bad_checksum";
        case R::Command:
            return "command";
        case R::TooLong:
            return "too_long";
        case R::BadLine:
            return "bad_line";
        case R::Setting:
            return "movie_setting";
        case R::Archive:
            return "archive";
        case R::Subframe:
            return "subframe";
        case R::System:
            return "system";
        case R::ChdUnsupported:
            return "rom_chd";
        case R::None:
        case R::kCount:
            return "movie";
    }
    return "movie";
}

const char* tasty_refusal_sentence(std::string_view why) noexcept {
    struct Row {
        std::string_view why;
        const char* sentence;
    };
    static constexpr Row kRows[] = {
        {"not_a_movie",
         "this file is not a movie tasty can read; check that it is the movie itself and complete"},
        {"savestate", "this movie starts from a savestate or saved game, not from power-on"},
        {"binary", "this .fm2 keeps its input in binary; save it again as text from FCEUX"},
        {"disk", "this movie is for a disk add-on such as the Famicom Disk System, which tasty "
                 "does not play"},
        {"port_type", "this movie uses a controller other than the standard pad, which tasty does "
                      "not replay"},
        {"multitap", "this movie has more than two players (a Four Score or multitap), which tasty "
                     "does not replay"},
        {"bad_checksum", "the movie's ROM checksum is missing or unreadable, so tasty cannot "
                         "confirm the ROM"},
        {"command", "this movie presses reset or power after it starts, which tasty does not "
                    "replay"},
        {"too_long", "this movie is longer than tasty can play"},
        {"bad_line", "a line of this movie's input cannot be read; the file may be damaged"},
        {"movie_setting", "this movie was recorded with an emulator setting, such as its region, "
                          "that this core cannot match"},
        {"archive", "this movie archive cannot be opened; check that the file is complete"},
        {"subframe", "this movie uses sub-frame input, which tasty does not replay"},
        {"system", "this movie is for another console than the one tasty runs it on"},
        {"rom_chd", "this build of tasty cannot read .chd disc images"},
        {"movie_io", "the movie file cannot be read"},
        {"rom_io", "the ROM file cannot be read; check the --rom path"},
        {"rom_size", "the ROM is larger than tasty hashes, or not a ROM this movie's format names"},
        {"no_codec", "no movie format tasty knows plays this file on this core"},
        {"lead", "the --lead value is outside what this movie's format allows"},
        {"phase", "--phase must be at least 1000 us and leave 2000 us before the next frame"},
    };
    for (const Row& r : kRows)
        if (r.why == why) return r.sentence;
    return nullptr;
}

int tasty_preflight_rom(const svc::Vfs& vfs, std::string_view movie, std::string_view rom) {
    const std::string why = cores::movie_unplayable_reason(vfs, movie);
    if (!why.empty()) {
        std::fprintf(stderr, "tasty: %s\n", why.c_str());
        return 1;
    }
    const auto sys = cores::movie_system_for(vfs, movie);
    if (!sys || sys->codec == nullptr) return 0;
    auto src = sys->codec->digest_source(vfs, rom);
    if (src) return 0;
    report_unhashable(*sys->codec, rom, src.error());
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
        const std::string why = cores::movie_unplayable_reason(vfs, movie);
        if (!why.empty()) {
            std::fprintf(stderr, "tasty: %s\n", why.c_str());
            return 1;
        }
        std::fprintf(stderr, "tasty: no codec for %.*s\n", static_cast<int>(movie.size()),
                     movie.data());
        return 1;
    }
    auto facts = cores::read_movie_facts(vfs, *sys->codec, movie);
    if (!facts) {
        const char* s =
            facts.error().code == Errc::bad_format
                ? tasty_refusal_sentence(tasty_movie_refusal_token(facts.error().detail))
                : nullptr;
        if (s != nullptr) {
            std::fprintf(stderr, "tasty: %s\n", s);
        } else {
            std::fprintf(stderr, "tasty: cannot read movie %.*s\n", static_cast<int>(movie.size()),
                         movie.data());
        }
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
        report_unhashable(*sys->codec, rom, src.error());
        return 1;
    }
    const auto got = digest_of(vfs, *src, facts->digest.kind);
    if (!got.value) {
        if (got.fail == DigestFail::Open)
            std::fprintf(stderr, "tasty: cannot open rom %.*s\n", static_cast<int>(rom.size()),
                         rom.data());
        else
            std::fprintf(stderr, got.fail == DigestFail::Short ? "tasty: short rom read\n"
                                                               : "tasty: rom read failed\n");
        return 1;
    }
    if (digest_matches(*sys->codec, got, *facts)) {
        std::printf("rom matches\n");
        return 0;
    }
    std::printf("rom mismatch\n");
    return 3;
}

int tasty_prepare_play(const svc::Vfs& vfs, TastyArgs& a) {

    if (a.loop && a.record && !a.record->empty()) {
        std::fprintf(stderr, "tasty: --loop and --record do not mix; drop --loop to record one "
                             "pass\n");
        return 1;
    }
    const auto sys = cores::movie_system_for(vfs, a.movie.view());
    if (!sys || sys->codec == nullptr) {
        std::fprintf(stderr, "tasty: no codec for %s\n", a.movie.c_str());
        return 1;
    }
    if (a.ram_fill && !sys->codec->ram_fill_need(*a.ram_fill)) {
        std::fprintf(stderr, "tasty: this movie's core has no RAM-init setting; --ram-init works "
                             "only where it has one (NES: RAM Clear)\n");
        return 1;
    }
    auto facts = cores::read_movie_facts(vfs, *sys->codec, a.movie.view());
    if (!facts) {
        std::fprintf(stderr, "tasty: cannot read movie %s\n", a.movie.c_str());
        return 1;
    }
    const std::int32_t lead = a.lead.value_or(sys->codec->default_lead(*facts));
    const auto range = sys->codec->lead_range();
    constexpr auto kArmMin = static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min());
    constexpr auto kArmMax = static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::max());
    if (lead < range.min || lead > range.max || lead < kArmMin || lead > kArmMax) {
        std::fprintf(stderr,
                     "tasty: --lead %d is outside %d..%d for this movie; "
                     "pass a lead inside that range\n",
                     static_cast<int>(lead), static_cast<int>(range.min),
                     static_cast<int>(range.max));
        return 1;
    }
    if (a.phase_us) {
        if (!app::ReplayFeeder::phase_fits(*a.phase_us, sys->codec->raster(*facts).period_ns)) {
            std::fprintf(stderr, "tasty: %s\n", tasty_refusal_sentence("phase"));
            return 1;
        }
    }
    if (a.rom.empty()) {
        const std::string_view named = sys->codec->games_folder();
        const std::string_view folder = named.empty() ? sys->conf_str_name : named;
        return lookup_rom(vfs, *sys->codec, *facts, folder, a.rom);
    }
    return tasty_preflight_rom(vfs, a.movie.view(), a.rom.view());
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
    const LogCount count = count_log_frames(vfs, *sys->codec, movie, *facts);
    std::printf(count.capped ? "frames %u or more\n" : "frames %u\n", count.frames);
    if (facts->has_rerecords)
        std::printf("rerecords %u\n", facts->rerecords);
    else
        std::printf("rerecords unknown\n");
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
    if (!movie || !rom || !core) return false;
    a.movie = *movie;
    a.rom = *rom;
    a.core = *core;
    if (!a.record) return true;
    const auto record = one(*a.record);
    if (!record) return false;
    a.record = *record;
    return true;
}

std::optional<app::PathText> tasty_prepare_record(app::PathText record,
                                                  std::string_view movie) noexcept {
    std::string path(record.view());
    const bool trailing = !path.empty() && path.back() == '/';
    if (trailing) path.pop_back();
    auto refuse = [](const char* why, const std::string& p) -> std::optional<app::PathText> {
        std::fprintf(stderr, "tasty: %s (%s)\n", why, p.c_str());
        return std::nullopt;
    };
    if (path.empty())
        return refuse("that record path names no directory or file", std::string(record.view()));

    if (!trailing && ends_with_avi(path)) {
        if (is_dir(path.c_str()))
            return refuse("that .avi path is a directory; pass a file name or another directory",
                          path);
        const auto slash = path.rfind('/');
        const std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
        if (base.size() <= 4)
            return refuse("that .avi name has no file stem; pass a name before .avi", path);
        path.replace(path.size() - 4, 4, ".avi");
        if (slash != std::string::npos && slash > 0 && !tasty_mkdir_p(path.substr(0, slash)))
            return refuse("cannot create the directory for that record", path);
        if (!record.assign(path)) return refuse("that record path is too long", path);
        return record;
    }
    if (is_file(path.c_str()))
        return refuse("that record path is a file; pass a directory or a name ending in .avi",
                      path);
    if (!tasty_mkdir_p(path)) return refuse("cannot create that record directory", path);
    if (movie.empty()) {
        path += '/';
        if (!record.assign(path)) return refuse("that record path is too long", path);
        return record;
    }
    const std::string joined = path + "/" + std::string(stem_of(movie));
    if (!record.assign(joined)) return refuse("that record path is too long", path);
    return record;
}

void warn_writeback_cpumask(const os::WritebackProbe& wb, int rt_cpu, const char* prog,
                            std::FILE* out) noexcept {
    if (!wb.fs_magic || !os::is_network_fs(*wb.fs_magic)) return;
    const auto cpus = os::effective_writeback_cpus(wb);
    if (!cpus || rt_cpu < 0 || rt_cpu >= 64) return;
    const std::uint64_t rt_bit = std::uint64_t{1} << rt_cpu;
    if ((*cpus & rt_bit) == 0) return;
    std::uint64_t fix = *wb.cpumask & ~rt_bit;
    if (fix == 0) fix = rt_cpu == 0 ? 2u : 1u;
    std::fprintf(out,
                 "%s: warning: writeback cpumask=%llx: flushing to a network share may run on "
                 "real-time CPU %d and stall real-time I/O; fix: echo %llx > %s\n",
                 prog, static_cast<unsigned long long>(*wb.cpumask), rt_cpu,
                 static_cast<unsigned long long>(fix), os::kWritebackCpumaskPath);
}

std::string_view tasty_record_dir(std::string_view record) noexcept {
    if (!record.empty() && record.back() == '/') {
        record.remove_suffix(1);
    } else {
        const auto slash = record.rfind('/');
        if (slash == std::string_view::npos) return ".";
        record = record.substr(0, slash);
    }
    return record.empty() ? std::string_view("/") : record;
}

void tasty_warn_record_writeback(std::string_view record, int rt_cpu, const char* cpumask_path,
                                 const char* unbound_path, std::FILE* out) noexcept {
    app::PathText dir{};
    if (!dir.assign(tasty_record_dir(record))) return;
    warn_writeback_cpumask(os::read_writeback_probe(dir.c_str(), cpumask_path, unbound_path),
                           rt_cpu, "tasty", out);
}

void tasty_warn_record_writeback(std::string_view record, int rt_cpu) noexcept {
    tasty_warn_record_writeback(record, rt_cpu, os::kWritebackCpumaskPath, os::kUnboundCpumaskPath,
                                stderr);
}

}  // namespace mister::fw
