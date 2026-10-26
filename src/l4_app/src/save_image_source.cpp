// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/save_image_source.h"

#include "infra/byte_view.h"

namespace mister::app {

static_assert(SaveImageSource::kSlots == 7, "one backend initializer per announceable slot");

SaveImageSource::SaveImageSource(const svc::Vfs& vfs) noexcept
    : backends_{
          svc::FileSlotBackend{vfs},
          svc::FileSlotBackend{vfs},
          svc::FileSlotBackend{vfs},
          svc::FileSlotBackend{vfs},
          svc::FileSlotBackend{vfs},
          svc::FileSlotBackend{vfs},
          svc::FileSlotBackend{vfs}
      } {}

SaveImageSource::Binding* SaveImageSource::find(proto::SlotIndex slot) {
    if (slot.v >= kSlots)
        return nullptr;
    return &bindings_[slot.v];
}

const SaveImageSource::Binding* SaveImageSource::find(proto::SlotIndex slot) const {
    if (slot.v >= kSlots)
        return nullptr;
    return &bindings_[slot.v];
}

bool SaveImageSource::bind(
    proto::SlotIndex slot,
    std::string_view path,
    bool manual,
    const cores::BlankSaveSpec& blank
) noexcept {
    Binding* b = find(slot);
    if (b == nullptr)
        return false;

    if (b->attach != Attach::Detached)
        return false;
    b->exists = false;
    b->size = proto::FileSize{};
    b->blank = blank;
    b->manual = !path.empty() && manual;
    b->close_owed = false;
    if (path.empty()) {
        b->path.clear();
        return true;
    }
    if (!b->path.assign(path)) {
        b->path.clear();
        return false;
    }
    return true;
}

void SaveImageSource::unbind_all() {
    for (unsigned i = 0; i < kSlots; ++i) {
        (void)bind(proto::SlotIndex{static_cast<std::uint8_t>(i)}, {});
    }
}

std::string_view SaveImageSource::path_of(proto::SlotIndex slot) const {
    const Binding* b = find(slot);
    return b == nullptr ? std::string_view{} : b->path.view();
}

proto::SlotAttributes SaveImageSource::attributes(proto::SlotIndex slot) const {
    const Binding* b = find(slot);
    if (b == nullptr || b->path.view().empty())
        return proto::SlotAttributes{};
    const bool pre = !b->manual;
    return proto::SlotAttributes{
        .writable = pre || b->exists,
        .growable = pre,
        .deferred_create = pre && !b->exists
    };
}

bool SaveImageSource::manual(proto::SlotIndex slot) const noexcept {
    const Binding* b = find(slot);
    return b != nullptr && b->manual;
}

void SaveImageSource::owe_close(proto::SlotIndex slot) noexcept {
    TASTY_SEAT_BODY(SaveImageSource);
    if (Binding* b = find(slot); b != nullptr)
        b->close_owed = true;
}

bool SaveImageSource::close_owed(proto::SlotIndex slot) const noexcept {
    const Binding* b = find(slot);
    return b != nullptr && b->close_owed;
}

proto::FileSize SaveImageSource::size_of(proto::SlotIndex slot) const {
    const Binding* b = find(slot);
    if (b == nullptr || !b->exists)
        return proto::FileSize{};
    return b->size;
}

bool SaveImageSource::exists(proto::SlotIndex slot) const noexcept {
    const Binding* b = find(slot);
    return b != nullptr && b->exists;
}

void SaveImageSource::note_attached(
    proto::SlotIndex slot,
    bool exists,
    proto::FileSize size
) noexcept {
    TASTY_SEAT_BODY(SaveImageSource);
    Binding* b = find(slot);
    if (b == nullptr)
        return;
    b->exists = exists;
    b->size = exists ? size : proto::FileSize{};
    b->attach = Attach::Attached;
}

void SaveImageSource::note_detached(proto::SlotIndex slot) noexcept {
    TASTY_SEAT_BODY(SaveImageSource);
    Binding* b = find(slot);
    if (b == nullptr)
        return;
    b->exists = false;
    b->size = proto::FileSize{};

    b->path.clear();
    b->blank = cores::BlankSaveSpec{};
    b->manual = false;
    b->close_owed = false;
    b->attach = Attach::Detached;
}

SaveImageSource::Attach SaveImageSource::attach_state(proto::SlotIndex slot) const noexcept {
    const Binding* b = find(slot);
    return b == nullptr ? Attach::Detached : b->attach;
}

void SaveImageSource::mark(proto::SlotIndex slot, Attach state) noexcept {
    TASTY_SEAT_BODY(SaveImageSource);
    if (Binding* b = find(slot); b != nullptr)
        b->attach = state;
}

svc::IStorageBackend* SaveImageSource::backend(proto::SlotIndex slot) noexcept {
    if (slot.v >= kSlots)
        return nullptr;
    return &backends_[slot.v];
}

std::span<const std::byte> SaveImageSource::order(proto::SlotIndex slot) const noexcept {
    const Binding* b = find(slot);
    if (b == nullptr)
        return {};
    return infra::text_bytes(b->path.view());
}

bool SaveImageSource::fill_blank(
    proto::SlotIndex slot,
    proto::Lba lba,
    std::span<std::uint8_t> window
) {
    const Binding* b = find(slot);
    if (b == nullptr || !b->blank.declared())
        return false;
    cores::render_blank(b->blank, static_cast<std::uint64_t>(lba.v) * b->blank.lba_bytes, window);
    return true;
}

}
