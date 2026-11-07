// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

namespace mister::app {

template <class T>
struct Vec3 {
    T x{};
    T y{};
    T z{};

    friend constexpr bool operator==(const Vec3&, const Vec3&) = default;
};

using Vec3f = Vec3<float>;

struct Mat3 {
    std::array<Vec3<double>, 3> rows{};
};

[[nodiscard]] constexpr Vec3<double> mat3_apply(const Mat3& m, const Vec3<double>& v) noexcept {
    const auto row = [&v](const Vec3<double>& r) noexcept {
        return r.x * v.x + r.y * v.y + r.z * v.z;
    };
    return {row(m.rows[0]), row(m.rows[1]), row(m.rows[2])};
}

}
