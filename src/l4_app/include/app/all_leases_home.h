// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::app {

class AllLeasesHome {
public:
    AllLeasesHome(AllLeasesHome&&) noexcept = default;
    AllLeasesHome& operator=(AllLeasesHome&&) noexcept = default;
    AllLeasesHome(const AllLeasesHome&) = delete;
    AllLeasesHome& operator=(const AllLeasesHome&) = delete;
    ~AllLeasesHome() = default;

private:
    friend class CoreBuilder;
    AllLeasesHome() noexcept = default;
};

}
