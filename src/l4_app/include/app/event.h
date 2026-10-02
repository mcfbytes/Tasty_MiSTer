// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <tuple>

#include "app/types.h"
#include "app/ui_request.h"
#include "infra/error.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "infra/spsc_ring.h"

namespace mister::proto {
enum class CoreType : std::uint8_t;
}

namespace mister::app {

enum class InfoId : std::uint32_t {
    None = 0,
    CoreNotFound,
    CoreLoadFailed,
    ImageMountFailed,
    ImageUnmounted,
    OptionCleared,
    SaveWritten,
    SaveFailed,
    ScreenshotWritten,
    ScreenshotFailed,
    BluetoothUnavailable,
    FbTerminalRequired,
    CdBiosMissing,
    RomLoadFailed,
};

struct Event {
    TASTY_SEAT_EXEMPT(component);
    enum class Kind : std::uint8_t {
        CoreLoaded,
        SessionFailed,
        SdActivity,
        InfoRequest,
        ProgressUpdate,
        DeadlineMiss,
        RequestRefused,
        ConfStrOnlySession,
        SessionAdvisory,
        SessionEnded,
        RamImageDeclined,
        kCount,
    };
    enum class ConfStrOnlyCause : std::uint8_t { RegistryMiss, NoStorage };
    struct Head {
        EmitSite site{};
        std::uint8_t pad_[2]{};

        CorrelationTag tag{};
    };
    static constexpr std::size_t kStore = 4;

    struct CoreLoaded {
        static constexpr Kind kKind = Kind::CoreLoaded;
        proto::CoreType type{};
        bool mgl_capable = false;
        bool front_end = false;
    };
    struct SessionFailed {
        static constexpr Kind kKind = Kind::SessionFailed;
        Errc why{};
    };
    struct SdActivity {
        static constexpr Kind kKind = Kind::SdActivity;
        std::uint8_t slot = 0;
    };
    struct InfoRequest {
        static constexpr Kind kKind = Kind::InfoRequest;
        InfoId id = InfoId::None;
    };
    struct ProgressUpdate {
        static constexpr Kind kKind = Kind::ProgressUpdate;
        std::uint16_t cur = 0;
        std::uint16_t max = 0;
    };
    struct DeadlineMiss {
        static constexpr Kind kKind = Kind::DeadlineMiss;
        std::uint16_t slot = 0;
    };
    struct RequestRefused {
        static constexpr Kind kKind = Kind::RequestRefused;
        UiRequest::Kind which{};
        std::uint8_t pad_ = 0;
        Errc why{};
    };
    struct ConfStrOnlySession {
        static constexpr Kind kKind = Kind::ConfStrOnlySession;
        ConfStrOnlyCause cause{};
    };
    struct SessionAdvisory {
        static constexpr Kind kKind = Kind::SessionAdvisory;
        Errc why{};
    };
    struct SessionEnded {
        static constexpr Kind kKind = Kind::SessionEnded;
    };

    struct RamImageDeclined {
        static constexpr Kind kKind = Kind::RamImageDeclined;

        Errc why{};
    };
    using Alternatives =
        std::tuple<CoreLoaded, SessionFailed, SdActivity, InfoRequest, ProgressUpdate, DeadlineMiss,
                   RequestRefused, ConfStrOnlySession, SessionAdvisory, SessionEnded,
                   RamImageDeclined>;

    Kind kind = Kind::CoreLoaded;
    std::uint8_t pad_[3]{};
    Head head{};
    alignas(4) std::array<std::byte, kStore> store{};
};
static_assert(infra::MessageSum<Event> && infra::HasHead<Event> &&
              infra::alternatives_are_total<Event>());
static_assert(sizeof(Event) == 16 && alignof(Event) == 4 && sizeof(Event::Head) == 8,
              "16 B: the head at 4, a 4-byte store every alternative fits");

enum class Delivery : std::uint8_t {
    Hint,
    Edge,
};

struct DeliveryRow {
    Event::Kind kind;
    Delivery delivery;
};

inline constexpr auto kDeliveryTable = std::to_array<DeliveryRow>({
    {Event::Kind::CoreLoaded, Delivery::Edge},
    {Event::Kind::SessionFailed, Delivery::Edge},
    {Event::Kind::SdActivity, Delivery::Hint},
    {Event::Kind::InfoRequest, Delivery::Edge},
    {Event::Kind::ProgressUpdate, Delivery::Edge},
    {Event::Kind::DeadlineMiss, Delivery::Edge},
    {Event::Kind::RequestRefused, Delivery::Edge},
    {Event::Kind::ConfStrOnlySession, Delivery::Edge},
    {Event::Kind::SessionAdvisory, Delivery::Edge},
    {Event::Kind::SessionEnded, Delivery::Edge},
    {Event::Kind::RamImageDeclined, Delivery::Edge},
});
static_assert(infra::rows_are_ordinal(kDeliveryTable),
              "every Event::Kind needs exactly one kDeliveryTable row, at its "
              "own ordinal: a new kind must DECLARE whether it is re-syncable");
inline constexpr std::size_t kDeliveryRows = kDeliveryTable.size();

constexpr Delivery delivery_of(Event::Kind k) noexcept {
    const auto idx = infra::ordinal(k);
    return idx < kDeliveryRows ? kDeliveryTable[idx].delivery : Delivery::Edge;
}

class EventQueue {
    TASTY_SEAT_MEDIATOR(Any, Ui);

public:
    static constexpr std::size_t kCapacity = 256;

    static constexpr std::size_t kKinds = static_cast<std::size_t>(Event::Kind::kCount);
    static_assert(kKinds == 11, "Event::Kind changed cardinality: audit loss_[] consumers "
                                "and update this pin");

    [[nodiscard]] bool push(const Event& e) noexcept;

    std::optional<Event> take() noexcept { return ring_.pop(); }

    std::uint32_t losses(Event::Kind k) const noexcept;

    std::uint32_t losses(Delivery d) const noexcept;
    std::size_t depth() const noexcept { return ring_.size(); }

private:
    xthread::SpscRing<Event, kCapacity> ring_{};
    std::uint32_t loss_[kKinds]{};
};

}  // namespace mister::app
