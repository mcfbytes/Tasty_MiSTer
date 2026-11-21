// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "svc/file.h"
#include "svc/rename_files.h"

namespace mister::svc {

class Vfs;

class CardWriter {
    TASTY_SEAT_EXEMPT(component);

public:
    explicit CardWriter(const Vfs& card) noexcept : card_(card) {}

    [[nodiscard]] Ex<std::unique_ptr<IFile>> open(std::string_view path, OpenMode mode);
    [[nodiscard]] Ex<void> ensure_dir(std::string_view rel);
    [[nodiscard]] Ex<void> replace(const RenameFiles& names);

    void sync_parent_dir(std::string_view path) noexcept;

    [[nodiscard]] Ex<void> replace_file(std::string_view rel, std::span<const std::byte> bytes);

    [[nodiscard]] Ex<void> remove(std::string_view rel);

private:
    const Vfs& card_;
};

}
