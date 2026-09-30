// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

#include "infra/error.h"

namespace mister::svc {

class Vfs;

template <class T, class Codec>
class PerCoreSetting {
public:
    explicit PerCoreSetting(std::string_view suffix) : suffix_(suffix) {}

    std::string path_for(std::string_view core) const {
        std::string s;
        s.append(core);
        s.push_back('_');
        s.append(suffix_);
        s.append(".cfg");
        return s;
    }

    Ex<T> load(const Vfs& vfs, std::string_view core) const;
    Ex<void> save(const Vfs& vfs, std::string_view core, const T& v) const;

    const T& value() const noexcept { return value_; }

private:
    std::string suffix_;
    T value_{};
};

}  // namespace mister::svc
