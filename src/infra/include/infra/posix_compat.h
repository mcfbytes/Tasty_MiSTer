// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdio>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#if !defined(__GLIBC__)
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#ifndef M_TRIM_THRESHOLD
#define M_TRIM_THRESHOLD 0
#define M_MMAP_MAX 0
#endif

inline int mallopt(int, int) { return 0; }

inline int pthread_attr_setaffinity_np(pthread_attr_t*, std::size_t, const cpu_set_t*) {
    return ENOSYS;
}
#endif

#if defined(__GLIBC__)
inline constexpr bool kPosixGlibc = true;
#else
inline constexpr bool kPosixGlibc = false;
#endif

inline void set_stdout_file(std::FILE* f) noexcept {
#if defined(__GLIBC__)
    stdout = f;
#else

    static int original = -1;
    if (f == nullptr) return;
    const int fd = ::fileno(f);
    if (fd == STDOUT_FILENO) {
        if (original >= 0) (void)::dup2(original, STDOUT_FILENO);
        return;
    }
    if (original < 0) original = ::fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 3);
    (void)::dup2(fd, STDOUT_FILENO);
#endif
}
