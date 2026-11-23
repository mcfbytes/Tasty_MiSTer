// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/x86/floppy_media.h"
#include "proto/rtc_calendar.h"

namespace mister::cores::x86 {

enum class X86RamSize : std::uint8_t { Mb256, Mb16 };

struct X86CmosInputs {
    proto::RtcCalendar calendar{};
    FloppyMedia fdd0{};
    FloppyMedia fdd1{};
    bool hdd0_named = false;
    bool hdd1_named = false;
    X86RamSize ram = X86RamSize::Mb16;
};

}
