// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "cores/core_profile.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "proto/block_geometry.h"
#include "proto/image_source.h"
#include "svc/file_slot_backend.h"
#include "svc/slot_path.h"
#include "svc/vfs.h"

namespace mister::app {

class SaveImageSource final : public proto::IImageSource {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr unsigned kSlots = proto::kMaxAnnounceableSlot + 1;

    enum class Attach : std::uint8_t { Detached, Attaching, Attached, Detaching };

    explicit SaveImageSource(const svc::Vfs& vfs) noexcept;

    [[nodiscard]] bool bind(proto::SlotIndex slot, std::string_view path,
                            bool manual = false) noexcept;
    std::string_view path_of(proto::SlotIndex slot) const;

    [[nodiscard]] bool manual(proto::SlotIndex slot) const noexcept;

    void owe_close(proto::SlotIndex slot) noexcept;
    [[nodiscard]] bool close_owed(proto::SlotIndex slot) const noexcept;

    void unbind_all();

    void set_blank_pattern(proto::SlotIndex slot, const cores::BlankSaveSpec& spec);

    proto::SlotAttributes attributes(proto::SlotIndex slot) const override;

    bool fill_blank(proto::SlotIndex slot, proto::Lba lba, std::span<std::uint8_t> window) override;

    void note_attached(proto::SlotIndex slot, bool exists, proto::FileSize size) noexcept override;
    void note_detached(proto::SlotIndex slot) noexcept override;

    static constexpr std::uint32_t kBlankBlockBytes = 512;

    proto::FileSize size_of(proto::SlotIndex slot) const;

    [[nodiscard]] Attach attach_state(proto::SlotIndex slot) const noexcept;

    void mark(proto::SlotIndex slot, Attach state) noexcept;

    [[nodiscard]] svc::IStorageBackend* backend(proto::SlotIndex slot) noexcept;

private:
    struct Binding {
        svc::SlotPath path{};
        bool exists = false;
        proto::FileSize size{};
        Attach attach = Attach::Detached;
        bool manual = false;
        bool close_owed = false;
        cores::BlankSaveSpec blank{};
    };
    Binding* find(proto::SlotIndex slot);
    const Binding* find(proto::SlotIndex slot) const;

    Binding bindings_[kSlots]{};

    svc::FileSlotBackend backends_[kSlots];
};

}  // namespace mister::app
