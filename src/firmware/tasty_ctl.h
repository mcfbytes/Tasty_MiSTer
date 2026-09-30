// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "infra/error.h"
#include "tasty_cli.h"

namespace mister::svc {
class Vfs;
}

namespace mister::fw {

inline constexpr const char* kTastyLockPath = "/tmp/tasty.lock";
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
[[nodiscard]] Ex<void> tasty_stop_stock() noexcept;
[[nodiscard]] Ex<void> tasty_write_cmd(std::string_view line) noexcept;
[[nodiscard]] Ex<void> tasty_write_status(std::string_view json) noexcept;
[[nodiscard]] Ex<std::string> tasty_read_status() noexcept;
void tasty_clear_status() noexcept;
int tasty_spawn_stock() noexcept;
[[nodiscard]] bool tasty_mkdir_p(std::string_view path) noexcept;
[[nodiscard]] bool tasty_resolve_args(TastyArgs& a) noexcept;
[[nodiscard]] std::optional<app::PathText> tasty_prepare_record(app::PathText record,
                                                                std::string_view movie) noexcept;
void tasty_set_owner_comm(const char* comm) noexcept;

struct ReturnHome {
    int lock_fd = -1;
    bool armed = true;
    int* spawned = nullptr;
    ~ReturnHome() noexcept;
};

enum class TastyClientAct : std::uint8_t { Idle, Signaled, WroteFifo };

[[nodiscard]] TastyClientAct tasty_client_act(TastyVerb v) noexcept;

[[nodiscard]] bool tasty_rec_start_joins_owner() noexcept;
[[nodiscard]] int tasty_run_client(TastyVerb v) noexcept;
[[nodiscard]] int tasty_check_movie(const svc::Vfs& vfs, std::string_view movie,
                                    std::string_view rom);
[[nodiscard]] int tasty_preflight_rom(const svc::Vfs& vfs, std::string_view movie,
                                      std::string_view rom);
[[nodiscard]] int tasty_info_movie(const svc::Vfs& vfs, std::string_view movie);
void tasty_set_ctl_paths(const char* lock, const char* pid, const char* status) noexcept;

}  // namespace mister::fw
