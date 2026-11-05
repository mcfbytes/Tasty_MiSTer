// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/durable_write.h"

#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <string>
#include <utility>

#include "infra/persist.h"
#include "vfs_detail.h"

namespace mister::svc {

namespace detail {

std::atomic<std::uint64_t> g_dura_ticket TASTY_PERSIST(proc, vfs_dura_ticket){0};
std::atomic<std::uint64_t> g_dura_file_syncs TASTY_PERSIST(proc, vfs_dura_file_syncs){0};
std::atomic<std::uint64_t> g_dura_dir_syncs TASTY_PERSIST(proc, vfs_dura_dir_syncs){0};
std::atomic<std::uint64_t> g_dura_renames TASTY_PERSIST(proc, vfs_dura_renames){0};
std::atomic<std::uint64_t> g_dura_last_file_sync TASTY_PERSIST(proc, vfs_dura_last_file_sync){0};
std::atomic<std::uint64_t> g_dura_last_rename TASTY_PERSIST(proc, vfs_dura_last_rename){0};

namespace {

std::uint64_t dura_ticket() noexcept {
    return g_dura_ticket.fetch_add(1, std::memory_order_relaxed) + 1;
}

}

}

void DurableWrite::note_file_sync() noexcept {
    detail::g_dura_last_file_sync.store(detail::dura_ticket(), std::memory_order_relaxed);
    detail::g_dura_file_syncs.fetch_add(1, std::memory_order_relaxed);
}

DurableWrite::DurabilityLog DurableWrite::log() noexcept {
    DurabilityLog out;
    out.file_syncs = detail::g_dura_file_syncs.load(std::memory_order_relaxed);
    out.dir_syncs = detail::g_dura_dir_syncs.load(std::memory_order_relaxed);
    out.renames = detail::g_dura_renames.load(std::memory_order_relaxed);
    out.last_file_sync = detail::g_dura_last_file_sync.load(std::memory_order_relaxed);
    out.last_rename = detail::g_dura_last_rename.load(std::memory_order_relaxed);
    return out;
}

Ex<void> DurableWrite::replace(std::string_view tmp_rel, std::string_view final_rel) const {
    const std::string from = detail::compose(root_path_, tmp_rel);
    const std::string to = detail::compose(root_path_, final_rel);
    if (::rename(from.c_str(), to.c_str()) != 0) {
        const auto e = static_cast<std::uint32_t>(errno);
        return std::unexpected(Error{Errc::os, ERR_SITE(), e});
    }
    detail::g_dura_last_rename.store(detail::dura_ticket(), std::memory_order_relaxed);
    detail::g_dura_renames.fetch_add(1, std::memory_order_relaxed);

    sync_dir_of_(to);
    return {};
}

void DurableWrite::sync_parent_dir(std::string_view rel) const noexcept {
    sync_dir_of_(detail::compose(root_path_, rel));
}

void DurableWrite::sync_dir_of_(const std::string& to) const noexcept {
    std::string dir = to;
    const std::size_t slash = dir.rfind('/');
    dir = (slash == std::string::npos) ? std::string(".")
        : (slash == 0)                 ? std::string("/")
                                       : dir.substr(0, slash);
    if (const int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC); dfd >= 0) {
        const bool ok = ::fsync(dfd) == 0 || errno == EINVAL || errno == EROFS;
        (void)::close(dfd);
        if (ok)
            detail::g_dura_dir_syncs.fetch_add(1, std::memory_order_relaxed);
    }
}

}
