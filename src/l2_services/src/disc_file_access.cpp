// SPDX-License-Identifier: GPL-3.0-or-later
#include "infra/prelude.h"
#include "svc/disc_file_access.h"
#include "svc/vfs_image_opener.h"

#include <algorithm>

namespace mister::svc {

DiscFileAccess::DiscFileAccess(const Vfs& vfs)
    : owned_opener_(std::make_unique<VfsImageOpener>(vfs)),
      opener_(*owned_opener_) {}

Ex<std::unique_ptr<IFile>> DiscFileAccess::open_file(std::string_view path) const {
    Ex<std::unique_ptr<IFile>> f = opener_.open_read(path);
    if (f)
        (void)(*f)->advise(IFile::Access::Sequential, proto::kFileStart, FileSize{});
    return f;
}

Ex<std::string> DiscFileAccess::read_cue_text(std::string_view cue_path) const {
    auto cue = open_file(cue_path);
    if (!cue)
        return std::unexpected(cue.error());
    auto cue_size = (*cue)->size();
    if (!cue_size)
        return std::unexpected(cue_size.error());

    constexpr proto::FileSize kMaxCueBytes{100u * 1024u};
    const proto::FileSize want = cue_size->min(kMaxCueBytes);
    std::string text = want.zeroed<std::string>();
    if (!want.empty()) {
        auto rd = (*cue)->read_at(proto::kFileStart, std::as_writable_bytes(std::span<char>(text)));
        if (!rd)
            return std::unexpected(rd.error());
        proto::FileSize::of(*rd).resize_in(text);
    }
    return text;
}

}
