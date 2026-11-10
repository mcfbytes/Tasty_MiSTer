// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "svc/quirk_id.h"
#include "svc/types.h"

namespace mister::svc {

enum class NameMatch : std::uint8_t {
    Any = 0,
    Exact,
    Contains,
    ContainsCs,
};

enum class AdmitAction : std::uint8_t {
    Reject = 0,
    Assign,
};

struct AdmissionRule {
    Vid vid;
    Pid pid;
    const char* name;
    NameMatch name_match;
    AdmitAction action;
    QuirkId quirk;
    const char* why;
};

std::span<const AdmissionRule> admission_rules();

struct NodeFacts {
    Vid vid{};
    Pid pid{};
    std::string_view name{};
};

struct Admission {
    bool accept = true;
    QuirkId quirk = QuirkId::None;
};

Admission admit_node(const NodeFacts& f);

}
