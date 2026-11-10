// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::os {

inline constexpr const char* kWritebackCpumaskPath = "/sys/bus/workqueue/devices/writeback/cpumask";

inline constexpr const char* kUnboundCpumaskPath = "/sys/devices/virtual/workqueue/cpumask";

struct WritebackPaths {
    const char* dir = "/";
    const char* cpumask = kWritebackCpumaskPath;
    const char* unbound = kUnboundCpumaskPath;
};

}
