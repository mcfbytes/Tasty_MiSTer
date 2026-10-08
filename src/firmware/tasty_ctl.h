// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

#include "infra/error.h"
#include "tasty_cli.h"

namespace mister::svc {
class Vfs;
}
namespace mister::os {
struct WritebackProbe;
}

namespace mister::fw {

inline constexpr const char* kTastyLockPath = "/tmp/tasty.lock";

inline constexpr const char* kTastySaveRoot = "tasty/saves";
inline constexpr const char* kTastyPidPath = "/tmp/tasty.pid";
inline constexpr const char* kTastyStatusPath = "/tmp/tasty.status";
inline constexpr const char* kTastyStatusTmpPath = "/tmp/tasty.status.tmp";
inline constexpr const char* kMisterCmdPath = "/dev/MiSTer_cmd";
inline constexpr const char* kStockMisterPath = "/media/fat/MiSTer";
inline constexpr const char* kMenuRbfName = "menu.rbf";

[[nodiscard]] Ex<int> tasty_lock_owner() noexcept;
void tasty_unlock(int fd) noexcept;
[[nodiscard]] Ex<void> tasty_write_pid(int pid, std::string_view movie = {}) noexcept;
[[nodiscard]] std::optional<int> tasty_read_pid() noexcept;
[[nodiscard]] std::optional<std::string> tasty_read_movie() noexcept;
[[nodiscard]] std::string tasty_busy_text() noexcept;
[[nodiscard]] Ex<void> tasty_signal_owner() noexcept;

[[nodiscard]] Ex<bool> tasty_stop_stock() noexcept;
[[nodiscard]] Ex<void> tasty_write_cmd(std::string_view line) noexcept;
[[nodiscard]] Ex<void> tasty_write_status(std::string_view json) noexcept;
[[nodiscard]] Ex<std::string> tasty_read_status() noexcept;
void tasty_clear_status() noexcept;

int tasty_spawn_stock() noexcept;

void tasty_set_home_image(const char* exe, const char* arg) noexcept;
[[nodiscard]] bool tasty_mkdir_p(std::string_view path) noexcept;
[[nodiscard]] bool tasty_resolve_args(TastyArgs& a) noexcept;
[[nodiscard]] std::optional<app::PathText> tasty_prepare_record(app::PathText record,
                                                                std::string_view movie) noexcept;

void warn_writeback_cpumask(const os::WritebackProbe& wb, int rt_cpu, const char* prog,
                            std::FILE* out) noexcept;

[[nodiscard]] std::string_view tasty_record_dir(std::string_view record) noexcept;

void tasty_warn_record_writeback(std::string_view record, int rt_cpu) noexcept;

void tasty_warn_record_writeback(std::string_view record, int rt_cpu, const char* cpumask_path,
                                 const char* unbound_path, std::FILE* out) noexcept;
void tasty_set_owner_comm(const char* comm) noexcept;

void tasty_set_stock_comm(const char* comm) noexcept;

struct ReturnHome {
    int lock_fd = -1;
    bool armed = true;
    int* spawned = nullptr;
    ReturnHome() noexcept;
    ~ReturnHome() noexcept;
};

void tasty_say(std::string_view line) noexcept;

int tasty_launch_home() noexcept;

enum class TastyClientAct : std::uint8_t { Idle, Signaled, WroteFifo };

[[nodiscard]] TastyClientAct tasty_client_act(TastyVerb v) noexcept;

[[nodiscard]] bool tasty_rec_start_joins_owner() noexcept;
[[nodiscard]] int tasty_run_client(TastyVerb v) noexcept;

[[nodiscard]] const char* tasty_movie_refusal_token(std::uint32_t codec_refusal) noexcept;

[[nodiscard]] const char* tasty_refusal_sentence(std::string_view why) noexcept;
[[nodiscard]] int tasty_check_movie(const svc::Vfs& vfs, std::string_view movie,
                                    std::string_view rom);
[[nodiscard]] int tasty_preflight_rom(const svc::Vfs& vfs, std::string_view movie,
                                      std::string_view rom);

[[nodiscard]] int tasty_prepare_play(const svc::Vfs& vfs, TastyArgs& a);
[[nodiscard]] int tasty_info_movie(const svc::Vfs& vfs, std::string_view movie);
void tasty_set_ctl_paths(const char* lock, const char* pid, const char* status) noexcept;

}  // namespace mister::fw
