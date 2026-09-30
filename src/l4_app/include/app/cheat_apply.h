// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::app {

class ICheatApply {
public:
    virtual ~ICheatApply() = default;

    [[nodiscard]] virtual bool apply_cheats_counted() noexcept = 0;

protected:
    ICheatApply() = default;
    ICheatApply(const ICheatApply&) = default;
    ICheatApply& operator=(const ICheatApply&) = default;
};

}  // namespace mister::app
