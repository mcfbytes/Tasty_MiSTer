// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/admission_rule.h"

#include <string_view>

#include "infra/ascii.h"
#include "svc/types.h"

namespace mister::svc {

namespace {

constexpr Vid kVidSony{0x054C};
constexpr Vid kVidNintendo{0x057E};

constexpr AdmissionRule kAdmissionRules[] = {

    {kAnyVid,
     kAnyPid,
     "MiSTer virtual input",
     NameMatch::Exact,
     AdmitAction::Reject,
     QuirkId::None,
     "input.cpp:5207-5213 — skip our own uinput node (UINPUT_NAME)"},
    {kVidSony,
     kAnyPid,
     "Motion",
     NameMatch::Contains,
     AdmitAction::Reject,
     QuirkId::None,
     "input.cpp:5268-5273 — DualShock/DualSense Motion Sensors: don't use Accelerometer"},

    {kVidSony,
     Pid{0x05C4},
     "Touchpad",
     NameMatch::Contains,
     AdmitAction::Assign,
     QuirkId::Ds4Touch,
     "input.cpp:5280-5283 — DS4 v1 touchpad node"},
    {kVidSony,
     Pid{0x09CC},
     "Touchpad",
     NameMatch::Contains,
     AdmitAction::Assign,
     QuirkId::Ds4Touch,
     "input.cpp:5280-5283 — DS4 v2 touchpad node"},
    {kVidSony,
     Pid{0x0BA0},
     "Touchpad",
     NameMatch::Contains,
     AdmitAction::Assign,
     QuirkId::Ds4Touch,
     "input.cpp:5280-5283 — DS4 USB dongle touchpad node"},
    {kVidSony,
     Pid{0x0CE6},
     "Touchpad",
     NameMatch::Contains,
     AdmitAction::Assign,
     QuirkId::Ds4Touch,
     "input.cpp:5280-5283 — DualSense touchpad node (THE rig device)"},
    {kVidSony,
     Pid{0x0268},
     nullptr,
     NameMatch::Any,
     AdmitAction::Assign,
     QuirkId::Ds3,
     "input.cpp:5276 — DualShock 3"},
    {kVidSony,
     Pid{0x05C4},
     nullptr,
     NameMatch::Any,
     AdmitAction::Assign,
     QuirkId::Ds4,
     "input.cpp:5278-5279 — DS4 v1"},
    {kVidSony,
     Pid{0x09CC},
     nullptr,
     NameMatch::Any,
     AdmitAction::Assign,
     QuirkId::Ds4,
     "input.cpp:5278-5279 — DS4 v2"},
    {kVidSony,
     Pid{0x0BA0},
     nullptr,
     NameMatch::Any,
     AdmitAction::Assign,
     QuirkId::Ds4,
     "input.cpp:5278-5279 — DS4 USB dongle"},
    {kVidSony,
     Pid{0x0CE6},
     nullptr,
     NameMatch::Any,
     AdmitAction::Assign,
     QuirkId::Ds4,
     "input.cpp:5278-5279 — DualSense"},
    {kVidNintendo,
     Pid{0x0306},
     "Accelerometer",
     NameMatch::Contains,
     AdmitAction::Reject,
     QuirkId::None,
     "input.cpp:5295-5300 — Wiimote accelerometer node"},
    {kVidNintendo,
     Pid{0x0330},
     "Accelerometer",
     NameMatch::Contains,
     AdmitAction::Reject,
     QuirkId::None,
     "input.cpp:5295-5300 — Wii U pro accelerometer node"},
    {kVidNintendo,
     Pid{0x0306},
     "Motion Plus",
     NameMatch::Contains,
     AdmitAction::Reject,
     QuirkId::None,
     "input.cpp:5302-5308 — Wiimote Motion Plus node"},
    {kVidNintendo,
     Pid{0x0330},
     "Motion Plus",
     NameMatch::Contains,
     AdmitAction::Reject,
     QuirkId::None,
     "input.cpp:5302-5308 — Wii U pro Motion Plus node"},

    {kVidNintendo,
     kAnyPid,
     " IMU",
     NameMatch::ContainsCs,
     AdmitAction::Reject,
     QuirkId::None,
     "input.cpp:5322-5327 — Nintendo IMU node"},
    {Vid{0x3250},
     Pid{0x1001},
     nullptr,
     NameMatch::Any,
     AdmitAction::Assign,
     QuirkId::AtariVcs,
     "input.cpp:5484-5490 — Atari VCS joystick; `vcs_proc` is unported (item)"},
};

}

std::span<const AdmissionRule> admission_rules() {
    return kAdmissionRules;
}

Admission admit_node(const NodeFacts& f) {
    for (const AdmissionRule& r : admission_rules()) {
        if (r.vid != kAnyVid && r.vid != f.vid)
            continue;
        if (r.pid != kAnyPid && r.pid != f.pid)
            continue;
        switch (r.name_match) {
            case NameMatch::Any:
                break;
            case NameMatch::Exact:
                if (f.name != std::string_view(r.name))
                    continue;
                break;
            case NameMatch::Contains:
                if (!infra::icontains(f.name, r.name))
                    continue;
                break;
            case NameMatch::ContainsCs:
                if (f.name.find(r.name) == std::string_view::npos)
                    continue;
                break;
        }
        if (r.action == AdmitAction::Reject)
            return Admission{false, QuirkId::None};
        return Admission{true, r.quirk};
    }
    return Admission{true, QuirkId::None};
}

}
