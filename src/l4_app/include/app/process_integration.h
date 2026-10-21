// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

#include "infra/error.h"

namespace mister::app::proc {

[[nodiscard]] Ex<int> uartmode_script(std::uint8_t mode);
[[nodiscard]] Ex<void> mlinkutil_baud(std::uint32_t baud);
[[nodiscard]] Ex<void> mlinkutil_soundfont(std::string_view relative_path);

[[nodiscard]] Ex<std::string> midilink_soundfont();

static_assert(
    std::is_same_v<decltype(midilink_soundfont()), Ex<std::string>>,
    "item: midilink_soundfont() must return an OWNING "
    "Ex<std::string>, not a dangling Ex<std::string_view>"
);

[[nodiscard]] Ex<void> bluetoothd_hcireset();
[[nodiscard]] Ex<void> bluetoothd_renew();
[[nodiscard]] Ex<void> hciconfig_reset();
[[nodiscard]] Ex<void> btctl_disconnect(std::string_view mac);
[[nodiscard]] Ex<void> killall_sigint_bt();
[[nodiscard]] Ex<bool> bt_adapter_up();

[[nodiscard]] Ex<void> killall(const char* name);

void widen_ui_affinity() noexcept;
void restore_ui_affinity() noexcept;

[[nodiscard]] Ex<void> write_tmp_handoff(const char* path, std::string_view contents);

}
