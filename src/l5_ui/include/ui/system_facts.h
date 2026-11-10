// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/fixed_str.h"
#include "os/kernel_rt.h"
#include "proto/types.h"
#include "ui/session_facts.h"

namespace mister::ui {

using KernelRt = os::KernelRt;

struct SystemFacts {
    FixedStr<16, StrFit::Clip> version{};
    FixedStr<8, StrFit::Clip> os_version{};
    proto::FileSize avail{};
    bool storage_usb = false;
    bool storage_sd_fallback = false;
    bool usb_mounted = false;

    KernelRt kernel = KernelRt::Unknown;
};

}
