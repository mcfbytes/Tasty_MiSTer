// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "os/kernel_rt.h"

namespace mister::ui {

using KernelRt = os::KernelRt;

struct SystemFacts {
    char version[16] = {};
    char os_version[8] = {};
    std::uint64_t avail_bytes = 0;
    bool storage_usb = false;
    bool storage_sd_fallback = false;
    bool usb_mounted = false;

    bool core_loaded = false;
    bool front_end = false;

    char core_dir[32] = {};

    KernelRt kernel = KernelRt::Unknown;

    bool hd_switchable = false;

    bool in_hd_tree = false;
};

}
