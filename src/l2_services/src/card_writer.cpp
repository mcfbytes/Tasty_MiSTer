// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/card_writer.h"

#include <optional>
#include <string>

#include "infra/path_parts.h"
#include "svc/vfs.h"

namespace mister::svc {

Ex<std::unique_ptr<IFile>> CardWriter::open(std::string_view path, OpenMode mode) {
    return card_.open_write_(path, mode);
}

Ex<void> CardWriter::ensure_dir(std::string_view rel) {
    return card_.ensure_dir_(rel);
}

Ex<void> CardWriter::replace(const RenameFiles& names) {
    return card_.replace_(names);
}

void CardWriter::sync_parent_dir(std::string_view path) noexcept {
    card_.sync_parent_dir_(path);
}

Ex<void> CardWriter::replace_file(std::string_view rel, std::span<const std::byte> bytes) {
    if (rel.empty()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    if (const std::optional<std::string_view> dir = infra::nested_dir(rel)) {
        if (auto r = ensure_dir(*dir); !r)
            return r;
    }

    const std::string tmp = std::string(rel) + ".tmp";
    auto f = open(tmp, OpenMode::Truncate);
    if (!f)
        return std::unexpected(f.error());

    Ex<void> done = write_all(**f, proto::kFileStart, bytes);
    if (done)
        done = (*f)->flush();

    f->reset();

    if (done)
        done = replace({tmp, rel});

    if (!done)
        (void)remove(tmp);
    return done;
}

Ex<void> CardWriter::remove(std::string_view rel) {
    return card_.remove_(rel);
}

}
