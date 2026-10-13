// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/process_title.h"

#include <cstring>
#include <memory>

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/lsan_interface.h>
#endif

namespace mister::os {

namespace {

char* region_end(int argc, char** argv, char** env) noexcept {
    char* end = argv[0] + std::strlen(argv[0]) + 1;
    for (int i = 1; i < argc && argv[i] != nullptr && argv[i] == end; ++i)
        end = argv[i] + std::strlen(argv[i]) + 1;
    for (int i = 0; env != nullptr && env[i] != nullptr && env[i] == end; ++i)
        end = env[i] + std::strlen(env[i]) + 1;
    return end;
}

}  // namespace

Ex<ProcessTitle> ProcessTitle::create(int argc, char** argv, char**& env) noexcept {
    if (argc <= 0 || argv == nullptr || argv[0] == nullptr)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    char* const start = argv[0];
    const char* const end = region_end(argc, argv, env);

    std::size_t env_bytes = 0;
    int env_count = 0;
    for (; env != nullptr && env[env_count] != nullptr; ++env_count)
        env_bytes += std::strlen(env[env_count]) + 1;
    if (env_count != 0) {
        std::unique_ptr<char[]> arena = std::make_unique<char[]>(env_bytes);
        std::size_t off = 0;
        for (int i = 0; i < env_count; ++i) {
            const std::size_t n = std::strlen(env[i]) + 1;
            std::memcpy(arena.get() + off, env[i], n);
            env[i] = arena.get() + off;
            off += n;
        }
        char* const kept = arena.release();
#if defined(__SANITIZE_ADDRESS__)
        __lsan_ignore_object(kept);
#else
        (void)kept;
#endif
    }

    return ProcessTitle{std::span<char>{start, static_cast<std::size_t>(end - start)}};
}

void ProcessTitle::set(std::string_view title) noexcept {
    TASTY_SEAT_BODY(ProcessTitle);
    const std::size_t cap = region_.size();
    if (cap == 0) return;
    std::size_t n = title.size();
    if (n + 1 > cap) {
        n = cap - 1;
        ++truncations_;
    }
    std::memcpy(region_.data(), title.data(), n);
    region_[n] = '\0';
    std::memset(region_.data() + n + 1, 0, cap - n - 1);
}

}  // namespace mister::os
