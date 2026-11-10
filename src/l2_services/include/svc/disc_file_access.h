// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "infra/error.h"
#include "infra/seat.h"
#include "svc/file.h"
#include "svc/image_opener.h"
#include "svc/vfs.h"

namespace mister::svc {

class DiscFileAccess final {
    TASTY_SEAT_RESIDENT(Io);

public:
    explicit DiscFileAccess(const IImageOpener& opener) noexcept : opener_(opener) {}

    explicit DiscFileAccess(const Vfs& vfs);

    [[nodiscard]] Ex<std::unique_ptr<IFile>> open_file(std::string_view path) const;

    [[nodiscard]] Ex<std::string> read_cue_text(std::string_view cue_path) const;

private:
    std::unique_ptr<IImageOpener> owned_opener_;
    const IImageOpener& opener_;
};

}
