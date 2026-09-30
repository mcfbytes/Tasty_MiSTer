// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/cmd_verb.h"

namespace mister::app {

class ICmdVerbSink {
public:
    virtual ~ICmdVerbSink() = default;

    [[nodiscard]] virtual bool on(const CmdVerb::LoadCore& v) noexcept = 0;
    [[nodiscard]] virtual bool on(const CmdVerb::Playlist& v) noexcept = 0;
    [[nodiscard]] virtual bool on(const CmdVerb::VideoMode& v) noexcept = 0;
    [[nodiscard]] virtual bool on(const CmdVerb::FbCmd& v) noexcept = 0;
    [[nodiscard]] virtual bool on(const CmdVerb::Screenshot& v) noexcept = 0;
    [[nodiscard]] virtual bool on(const CmdVerb::Volume& v) noexcept = 0;
    [[nodiscard]] virtual bool on(const CmdVerb::RtStats& v) noexcept = 0;
    [[nodiscard]] virtual bool on(const CmdVerb::TasPlay& v) noexcept = 0;
    [[nodiscard]] virtual bool on(const CmdVerb::TasStop& v) noexcept = 0;
    [[nodiscard]] virtual bool on(const CmdVerb::RecStart& v) noexcept = 0;
    [[nodiscard]] virtual bool on(const CmdVerb::RecArm& v) noexcept = 0;
    [[nodiscard]] virtual bool on(const CmdVerb::RecStop& v) noexcept = 0;
    [[nodiscard]] virtual bool on(const CmdVerb::RecDisarm& v) noexcept = 0;

protected:
    ICmdVerbSink() = default;
    ICmdVerbSink(const ICmdVerbSink&) = default;
    ICmdVerbSink& operator=(const ICmdVerbSink&) = default;
};

}  // namespace mister::app
