// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/replace_file.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <string>
#include <utility>

#include "infra/prelude.h"
#include "os/file_create.h"
#include "os/sync.h"
#include "os/write_all.h"

namespace mister::os {

namespace {

[[nodiscard]] std::unexpected<Error> abandon(const std::string& tmp, Error first) noexcept {
    (void)::unlink(tmp.c_str());
    return std::unexpected(first);
}

}

[[nodiscard]] Ex<void> replace_file(
    const infra::RenameFiles& names,
    std::span<const std::byte> bytes,
    infra::FileMode mode,
    ReplaceSync durability
) noexcept {
    const std::string tmp(names.tmp);
    const std::string dest(names.dest);
    constexpr OpenFlags kTmpFlags = OpenFlags::write_only().creating().truncating();
    static_assert(kTmpFlags.open_word() == (O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC));
    auto fd = open_created(tmp.c_str(), kTmpFlags, mode);
    if (!fd)
        return std::unexpected(fd.error().refusal(Errc::os, ERR_SITE()));
    {

        const UniqueFd guard{std::move(*fd)};
        if (const auto wrote = write_all(guard, bytes); !wrote)
            return abandon(tmp, wrote.error());
        if (durability == ReplaceSync::File) {
            if (const auto synced = sync(guard, SyncKind::All); !synced) {
                return abandon(tmp, synced.error());
            }
        }
    }
    if (::rename(tmp.c_str(), dest.c_str()) != 0)
        return abandon(tmp, infra::errno_error(Errc::os, ERR_SITE(), errno));
    return {};
}

[[nodiscard]] Ex<void> replace_file(
    const infra::RenameFiles& names,
    std::string_view text,
    infra::FileMode mode,
    ReplaceSync durability
) noexcept {
    return replace_file(names, std::as_bytes(std::span(text)), mode, durability);
}

}
