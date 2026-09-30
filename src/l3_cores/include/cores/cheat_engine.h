// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::cores {

class ICheatEngine {
public:
    virtual ~ICheatEngine() = default;

    virtual void service_cheat_tick() noexcept = 0;

protected:
    ICheatEngine() = default;
    ICheatEngine(const ICheatEngine&) = default;
    ICheatEngine& operator=(const ICheatEngine&) = default;
};

}  // namespace mister::cores
