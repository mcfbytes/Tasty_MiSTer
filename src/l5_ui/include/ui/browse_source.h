// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "infra/error.h"
#include "svc/vfs.h"

namespace mister::ui {

class IBrowseSource {
public:
    virtual ~IBrowseSource() = default;

    [[nodiscard]] virtual Ex<std::vector<svc::DirEntry>> scan(
        std::string_view dir,
        const svc::ScanFilter& f
    ) const = 0;
    [[nodiscard]] virtual Ex<std::string> resolve(
        std::string_view rel,
        const svc::SearchPolicy& p
    ) const = 0;
    virtual bool dir_exists(std::string_view path) const = 0;
    virtual bool file_exists(std::string_view path) const = 0;

    virtual std::string_view root_path() const = 0;

protected:
    IBrowseSource() = default;
    IBrowseSource(const IBrowseSource&) = default;
    IBrowseSource& operator=(const IBrowseSource&) = default;
};

}
