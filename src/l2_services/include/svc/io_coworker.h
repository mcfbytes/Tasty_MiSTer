// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::svc {

enum class LaneClass : std::uint8_t { Deadline, Bulk };

template <class L>
inline constexpr LaneClass lane_class_of = LaneClass::Deadline;
template <class L>
    requires requires { L::kLaneClass; }
inline constexpr LaneClass lane_class_of<L> = L::kLaneClass;

class IIoCoworker {
public:
    virtual ~IIoCoworker() = default;
    IIoCoworker(const IIoCoworker&) = delete;
    IIoCoworker& operator=(const IIoCoworker&) = delete;
    IIoCoworker(IIoCoworker&&) = delete;
    IIoCoworker& operator=(IIoCoworker&&) = delete;

    virtual void serve() noexcept = 0;
    [[nodiscard]] virtual bool idle() const noexcept = 0;

    [[nodiscard]] virtual bool quiescent() const noexcept { return idle(); }

    virtual void on_pause() noexcept {}

    virtual void on_resume() noexcept {}

    virtual void on_stop() noexcept {}

protected:
    IIoCoworker() = default;
};

}
