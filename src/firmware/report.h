// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "infra/error.h"

namespace mister::fw {

class IReportVoice {
public:
    virtual void say(std::string_view line) const noexcept = 0;

protected:
    ~IReportVoice() = default;
};

void set_report_voice(const IReportVoice* voice) noexcept;

void report(const char* what, const Error& e);

}  // namespace mister::fw
