// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdlib.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace mister::testing {

inline std::string temp_base() {
    std::error_code ec;
    std::string base = std::filesystem::temp_directory_path(ec).string();
    return ec || base.empty() ? std::string("/tmp") : base;
}

class TempRoot {
public:
    explicit TempRoot(std::string_view stem) {
        base_ = temp_base();
        std::string tmpl = base_ + "/" + std::string(stem) + "_XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        const char* made = ::mkdtemp(buf.data());
        if (made != nullptr)
            path_ = made;
    }

    ~TempRoot() noexcept { drop_(); }

    TempRoot(const TempRoot&) = delete;
    TempRoot& operator=(const TempRoot&) = delete;

    TempRoot(TempRoot&& other) noexcept
        : base_(std::move(other.base_)),
          path_(std::move(other.path_)) {
        other.path_.clear();
    }

    TempRoot& operator=(TempRoot&& other) noexcept {
        if (this != &other) {
            drop_();
            base_ = std::move(other.base_);
            path_ = std::move(other.path_);
            other.path_.clear();
        }
        return *this;
    }

    bool ok() const noexcept { return !path_.empty(); }

    const std::string& path() const noexcept { return path_; }

    std::string at(std::string_view rel) const { return path_ + "/" + std::string(rel); }

    std::string release() noexcept {
        std::string kept = std::move(path_);
        path_.clear();
        return kept;
    }

private:
    void drop_() noexcept {
        if (path_.empty())
            return;
        if (under_base_()) {
            std::error_code ec;
            std::filesystem::remove_all(path_, ec);
        } else {
            std::printf("TempRoot REFUSED %s: not under %s\n", path_.c_str(), base_.c_str());
        }
        path_.clear();
    }

    bool under_base_() const noexcept {
        return path_.size() > base_.size() + 1
            && path_.compare(0, base_.size(), base_) == 0
            && path_[base_.size()] == '/'
            && path_.find("/..") == std::string::npos;
    }

    std::string base_;
    std::string path_;
};

static_assert(
    !std::is_copy_constructible_v<TempRoot> && !std::is_copy_assignable_v<TempRoot>,
    "TempRoot is move-only: a copy would remove one tree twice"
);
static_assert(
    std::is_nothrow_move_constructible_v<TempRoot> && std::is_nothrow_destructible_v<TempRoot>,
    "TempRoot must be usable in a file-scope object that dies at exit"
);

}
