// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <string_view>

#include "infra/error.h"
#include "infra/seat.h"
#include "svc/file.h"
#include "svc/image_opener.h"
#include "svc/vfs.h"

namespace mister::svc {

class VfsImageOpener final : public IImageOpener {
    TASTY_SEAT_RESIDENT(Io);

public:
    explicit VfsImageOpener(const Vfs& vfs) noexcept : vfs_(vfs) {}

    [[nodiscard]] Ex<std::unique_ptr<IFile>> open_read(std::string_view path) const override {
        return vfs_.open(path, OpenMode::Read);
    }

private:
    const Vfs& vfs_;
};

}
