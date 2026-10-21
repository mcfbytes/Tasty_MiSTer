// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::proto {
class ItemTable;
}

namespace mister::ui {

struct BuildEnv;

class IHdRewalk {
public:
    virtual ~IHdRewalk() = default;

    virtual void rewalk(const proto::ItemTable& table, const BuildEnv& env) = 0;

protected:
    IHdRewalk() = default;
    IHdRewalk(const IHdRewalk&) = default;
    IHdRewalk& operator=(const IHdRewalk&) = default;
};

}
