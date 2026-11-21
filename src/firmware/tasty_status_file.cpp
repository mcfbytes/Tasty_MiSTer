// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_status_file.h"

#include "infra/file_mode.h"
#include "infra/prelude.h"
#include "infra/rename_files.h"
#include "os/replace_file.h"

namespace mister::fw {

Ex<void> TastyStatusFile::write(std::string_view json) const noexcept {
    const auto tmp = paths_.status_tmp();
    if (!tmp)
        return std::unexpected(tmp.error());
    return os::replace_file(
        infra::RenameFiles{tmp->view(), paths_.status.view()},
        json,
        infra::kSharedFileMode,
        os::ReplaceSync::None
    );
}

}
