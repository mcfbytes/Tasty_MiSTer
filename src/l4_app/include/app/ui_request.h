// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

#include "app/core_scope.h"
#include "app/xml_kind.h"
#include "app/path_text.h"
#include "app/types.h"
#include "cores/types.h"
#include "infra/fixed_str.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "proto/reset_edge.h"
#include "proto/types.h"

namespace mister::app {

struct UiRequest {
    TASTY_SEAT_EXEMPT(component);
    enum class Kind : std::uint8_t {
        LoadCore,
        SaveConfig,
        SaveDips,
        SaveCoreConfig,
        LoadCoreConfig,
        LoadFile,
        MountImage,
        UnmountImage,
        ResetCore,
        Reboot,
        LoadFileByDigit,
        kCount,
    };
    struct Head {
        CorrelationTag tag{};
    };
    static constexpr std::size_t kStore = kPathMax + 12;

    struct LoadCore {
        static constexpr Kind kKind = Kind::LoadCore;
        PathText path{};
        XmlKind xml = XmlKind::Rbf;
    };
    struct SaveConfig {
        static constexpr Kind kKind = Kind::SaveConfig;
    };
    struct SaveDips {
        static constexpr Kind kKind = Kind::SaveDips;
    };
    struct SaveCoreConfig {
        static constexpr Kind kKind = Kind::SaveCoreConfig;
        cores::ConfigSlot slot{};
        std::uint8_t pad_ = 0;
        CoreScope scope{};
    };
    struct LoadCoreConfig {
        static constexpr Kind kKind = Kind::LoadCoreConfig;
        cores::ConfigSlot slot{};
        std::uint8_t pad_ = 0;
        CoreScope scope{};
    };
    struct LoadFile {
        static constexpr Kind kKind = Kind::LoadFile;
        proto::IoIndex index{};
        std::uint8_t pad_ = 0;
        proto::ItemOrdinal item{};
        CoreScope scope{};
        PathText path{};
    };
    struct MountImage {
        static constexpr Kind kKind = Kind::MountImage;
        proto::IoIndex index{};
        std::uint8_t pad_ = 0;
        CoreScope scope{};
        PathText path{};
    };
    struct UnmountImage {
        static constexpr Kind kKind = Kind::UnmountImage;
        proto::IoIndex index{};
        std::uint8_t pad_ = 0;
        CoreScope scope{};
    };
    struct ResetCore {
        static constexpr Kind kKind = Kind::ResetCore;
        proto::ResetEdge edge{};
        CoreScope scope{};
    };
    struct Reboot {
        static constexpr Kind kKind = Kind::Reboot;
    };
    struct LoadFileByDigit {
        static constexpr Kind kKind = Kind::LoadFileByDigit;
        proto::FileSlotDigit digit{};
        std::uint8_t pad_ = 0;
        CoreScope scope{};
        PathText path{};
    };
    using Alternatives =
        std::tuple<LoadCore, SaveConfig, SaveDips, SaveCoreConfig, LoadCoreConfig, LoadFile,
                   MountImage, UnmountImage, ResetCore, Reboot, LoadFileByDigit>;

    Kind kind = Kind::LoadCore;
    std::uint8_t pad_[3]{};
    Head head{};
    alignas(4) std::array<std::byte, kStore> store{};
};
static_assert(infra::MessageSum<UiRequest> && infra::HasHead<UiRequest> &&
              infra::alternatives_are_total<UiRequest>());

static_assert(sizeof(UiRequest) == kPathMax + 20, "kind, pad, head and store");
static_assert(std::is_trivially_copyable_v<UiRequest>,
              "shape 1 carries no ownership: a pointer here would run a "
              "destructor on whichever seat happened to pop it");

}  // namespace mister::app
