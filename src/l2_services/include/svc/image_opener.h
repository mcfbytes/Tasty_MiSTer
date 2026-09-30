// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <string_view>

#include "infra/error.h"
#include "svc/vfs.h"

namespace mister::svc {

class IImageOpener {
public:
    virtual ~IImageOpener() = default;

    virtual Ex<std::unique_ptr<IFile>> open_read(std::string_view path) const = 0;

protected:
    IImageOpener() = default;
    IImageOpener(const IImageOpener&) = default;
    IImageOpener& operator=(const IImageOpener&) = default;
};

}  // namespace mister::svc
