// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "infra/error.h"
#include "infra/seat.h"
#include "svc/vfs.h"
#include "ui/browse_source.h"

namespace mister::ui {

class VfsBrowseSource final : public IBrowseSource {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit VfsBrowseSource(const svc::Vfs& vfs) noexcept : vfs_(&vfs) {}

    [[nodiscard]] Ex<std::vector<svc::DirEntry>> scan(
        std::string_view dir,
        const svc::ScanFilter& f
    ) const override {
        TASTY_SEAT_BODY(VfsBrowseSource);
        return vfs_->scan(dir, f);
    }

    [[nodiscard]] Ex<std::string> resolve(
        std::string_view rel,
        const svc::SearchPolicy& p
    ) const override {
        TASTY_SEAT_BODY(VfsBrowseSource);
        auto r = vfs_->resolve(rel, p);
        if (!r)
            return r;
        const std::string& root = vfs_->root_path();
        if (*r == root)
            return std::string{};
        if (r->size() > root.size() + 1
            && r->compare(0, root.size(), root) == 0
            && (*r)[root.size()] == '/') {
            return r->substr(root.size() + 1);
        }
        return r;
    }

    bool dir_exists(std::string_view path) const override {
        TASTY_SEAT_BODY(VfsBrowseSource);
        return vfs_->dir_exists(path);
    }

    bool file_exists(std::string_view path) const override {
        TASTY_SEAT_BODY(VfsBrowseSource);
        return vfs_->file_exists(path);
    }

    std::string_view root_path() const override {
        TASTY_SEAT_BODY(VfsBrowseSource);
        return vfs_->root_path();
    }

private:
    const svc::Vfs* vfs_;
};

}
