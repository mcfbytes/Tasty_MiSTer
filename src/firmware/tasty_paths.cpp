// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_paths.h"

#include <string>

namespace mister::fw {

namespace {

[[nodiscard]] infra::PathText must_fit(const char* path) noexcept {
    infra::PathText text{};
    if (!text.assign(path))
        fatal(Error{Errc::bad_format, ERR_SITE(), 0}, "tasty path");
    return text;
}

}

TastyPaths::TastyPaths() noexcept
    : lock{must_fit(kTastyLockPath)},
      pid{must_fit(kTastyPidPath)},
      status{must_fit(kTastyStatusPath)},
      cmd{must_fit(kMisterCmdPath)} {}

Ex<TastyPaths> TastyPaths::in(std::string_view dir) noexcept {
    TastyPaths paths{};
    const std::string base(dir);
    if (!paths.lock.assign(base + "/lock")
        || !paths.pid.assign(base + "/pid")
        || !paths.status.assign(base + "/status"))
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    return paths;
}

Ex<infra::PathText> TastyPaths::status_tmp() const noexcept {
    infra::PathText tmp{};
    if (!tmp.assign(std::string(status.view()) + ".tmp"))
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    return tmp;
}

}
