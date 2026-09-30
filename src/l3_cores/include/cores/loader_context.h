// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "infra/seat.h"
#include "svc/vfs.h"

namespace mister::cores {

class ILoader;
struct CoreProfile;
struct LoaderMemo;

struct LoaderContext {
    TASTY_SEAT_EXEMPT(const_shared);
    const svc::Vfs& vfs;
    const CoreProfile& profile;
    bool turbo = false;
    LoaderMemo* memo = nullptr;
};

using MakeLoader = std::unique_ptr<ILoader> (*)(const LoaderContext&);

}  // namespace mister::cores
