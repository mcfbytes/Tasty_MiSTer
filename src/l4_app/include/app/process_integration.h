// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

#include "infra/error.h"

namespace mister::app::proc {

Ex<void> uartmode_script(std::uint8_t mode, int* exit_code = nullptr);
Ex<void> mlinkutil_baud(std::uint32_t baud);
Ex<void> mlinkutil_soundfont(std::string_view relative_path);

Ex<std::string> midilink_soundfont();

static_assert(std::is_same_v<decltype(midilink_soundfont()), Ex<std::string>>,
              "item: midilink_soundfont() must return an OWNING "
              "Ex<std::string>, not a dangling Ex<std::string_view>");

Ex<void> bluetoothd_hcireset();
Ex<void> bluetoothd_renew();
Ex<void> hciconfig_reset();
Ex<void> btctl_disconnect(std::string_view mac);
Ex<void> killall_sigint_bt();
[[nodiscard]] Ex<bool> bt_adapter_up();

[[nodiscard]] Ex<void> killall(const char* name);

void widen_ui_affinity() noexcept;
void restore_ui_affinity() noexcept;

Ex<void> write_tmp_handoff(const char* path, std::string_view contents);

}  // namespace mister::app::proc
