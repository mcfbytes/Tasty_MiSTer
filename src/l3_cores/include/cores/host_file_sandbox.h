// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "infra/error.h"
#include "infra/prelude.h"
#include "svc/move_names.h"
#include "infra/seat.h"
#include "svc/dir_entry.h"
#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::cores {

class HostFileSandbox {
    TASTY_SEAT_RESIDENT(Io);

public:
    HostFileSandbox(const svc::Vfs& vfs, std::string_view root) : vfs_(&vfs), root_(root) {}

    [[nodiscard]] Ex<std::string> resolve(std::string_view guest_path) const;

    void rebind(std::string_view root) { root_.assign(root); }

    [[nodiscard]] std::string_view root() const noexcept { return root_; }

    [[nodiscard]] bool ready() const noexcept { return vfs_->dir_exists(root_); }

    [[nodiscard]] Ex<std::vector<svc::DirEntry>> list() const;

    [[nodiscard]] Ex<std::unique_ptr<svc::IFile>> open(std::string_view guest_path) const;
    [[nodiscard]] Ex<std::unique_ptr<svc::IFile>> create(std::string_view guest_path);

    enum class Kind : std::uint8_t { Absent, File, Dir };

    [[nodiscard]] Kind kind(std::string_view guest_path) const;
    [[nodiscard]] Ex<std::vector<svc::DirEntry>> list(std::string_view guest_dir) const;
    [[nodiscard]] Ex<std::chrono::sys_seconds> modified(std::string_view guest_path) const;

    [[nodiscard]] Ex<std::unique_ptr<svc::IFile>> open(
        std::string_view guest_path,
        svc::OpenMode mode
    );
    [[nodiscard]] Ex<void> remove(std::string_view guest_path);
    [[nodiscard]] Ex<void> remove_dir(std::string_view guest_path);
    [[nodiscard]] Ex<void> make_dir(std::string_view guest_path);

    [[nodiscard]] Ex<void> move(const svc::MoveNames& guest_names);
    [[nodiscard]] Ex<svc::Vfs::Space> space() const;

    [[nodiscard]] Ex<void> make_root();

private:
    const svc::Vfs* vfs_;
    std::string root_;
};

}
