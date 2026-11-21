// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <malloc.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#include <cstddef>
#include <cstdio>

namespace mister::infra {

using ::mallopt;
using ::pthread_attr_setaffinity_np;

inline void set_stdout_file(std::FILE* f) noexcept {
    stdout = f;
}

}
