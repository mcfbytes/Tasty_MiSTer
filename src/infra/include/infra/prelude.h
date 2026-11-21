// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/crc32.h"
#include "infra/errno.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "infra/seat_role.h"
#include "infra/service_set.h"
#include "infra/service_slot.h"
#include "infra/unique_fd.h"

namespace mister::infra::xthread {}

namespace mister {

namespace xthread = infra::xthread;
namespace seat_owner = infra::seat_owner;

using infra::Errc;
using infra::Error;
using infra::Ex;
using infra::fatal;
using infra::unimplemented;
using infra::errc_code;
using infra::SeatTag;
using infra::kSeatTagCount;
using infra::kSeatNames;
using infra::seat_name;
using infra::current_seat;
using infra::adopt_seat_tag;
using infra::SeatScope;
using infra::kSeatChecksEnabled;
using infra::seat_assert;
using infra::SeatRoleRow;
using infra::kSeatRoles;
using infra::seat_role;
using infra::StrFit;
using infra::FixedStr;
using infra::UniqueFd;
using infra::ServiceSlot;
using infra::ServiceSet;
using infra::kMaxServices;
using infra::kReflectedPolynomial;
using infra::crc32_update;
using infra::crc32_of;
using infra::crc32_header_skip;

}
