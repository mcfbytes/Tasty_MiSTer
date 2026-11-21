// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

#include "svc/input/device_match.h"

namespace mister::svc::input {

struct NoMergeRule {
    DeviceMatch match;
    const char* why;
};

std::span<const NoMergeRule> no_merge_rules();

}
