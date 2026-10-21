// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "svc/image_opener.h"
#include "svc/vfs.h"

namespace mister::svc {

class DiscFileAccess final {
    TASTY_SEAT_RESIDENT(Io);

public:
    DiscFileAccess(infra::OptRef<const Vfs> vfs, infra::OptRef<const IImageOpener> opener) noexcept
        : vfs_(vfs),
          opener_(opener) {}

    [[nodiscard]] Ex<std::unique_ptr<IFile>> open_file(std::string_view path) const;

    [[nodiscard]] Ex<std::string> read_cue_text(std::string_view cue_path) const;

private:
    infra::OptRef<const Vfs> vfs_;
    infra::OptRef<const IImageOpener> opener_;
};

}
