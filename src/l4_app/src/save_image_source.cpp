// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/save_image_source.h"

#include <utility>

#include "infra/byte_view.h"

namespace mister::app {
namespace {

template <std::size_t... I>
std::array<svc::FileSlotBackend, sizeof...(I)> make_backends(
    std::index_sequence<I...>,
    const svc::Vfs& vfs
) noexcept {
    return std::array<svc::FileSlotBackend, sizeof...(I)>{((void)I, svc::FileSlotBackend{vfs})...};
}

}

SaveImageSource::SaveImageSource(const svc::Vfs& vfs) noexcept
    : backends_{make_backends(std::make_index_sequence<kSlots>{}, vfs)} {}

infra::OptRef<SaveImageSource::Binding> SaveImageSource::find(proto::SlotIndex slot) {
    return bindings_.find(slot);
}

infra::OptRef<const SaveImageSource::Binding> SaveImageSource::find(proto::SlotIndex slot) const {
    return bindings_.find(slot);
}

bool SaveImageSource::bind(
    proto::SlotIndex slot,
    std::string_view path,
    bool manual,
    const svc::BlankSaveSpec& blank
) noexcept {
    const auto b = find(slot);
    if (!b)
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
        (void)bind(proto::SlotIndex::of(i), {});
    }
}

std::string_view SaveImageSource::path_of(proto::SlotIndex slot) const {
    const auto b = find(slot);
    return b ? b->path.view() : std::string_view{};
}

proto::SlotAttributes SaveImageSource::attributes(proto::SlotIndex slot) const {
    const auto b = find(slot);
    if (!b || b->path.view().empty())
        return proto::SlotAttributes{};
    const bool pre = !b->manual;
    return proto::SlotAttributes{
        .writable = pre || b->exists,
        .growable = pre,
        .deferred_create = pre && !b->exists
    };
}

bool SaveImageSource::manual(proto::SlotIndex slot) const noexcept {
    const auto b = find(slot);
    return b.has_value() && b->manual;
}

void SaveImageSource::owe_close(proto::SlotIndex slot) noexcept {
    TASTY_SEAT_BODY(SaveImageSource);
    if (const auto b = find(slot))
        b->close_owed = true;
}

bool SaveImageSource::close_owed(proto::SlotIndex slot) const noexcept {
    const auto b = find(slot);
    return b.has_value() && b->close_owed;
}

proto::FileSize SaveImageSource::size_of(proto::SlotIndex slot) const {
    const auto b = find(slot);
    if (!b || !b->exists)
        return proto::FileSize{};
    return b->size;
}

bool SaveImageSource::exists(proto::SlotIndex slot) const noexcept {
    const auto b = find(slot);
    return b.has_value() && b->exists;
}

void SaveImageSource::note_attached(
    proto::SlotIndex slot,
    bool exists,
    proto::FileSize size
) noexcept {
    TASTY_SEAT_BODY(SaveImageSource);
    const auto b = find(slot);
    if (!b)
        return;
    b->exists = exists;
    b->size = exists ? size : proto::FileSize{};
    b->attach = Attach::Attached;
}

void SaveImageSource::note_detached(proto::SlotIndex slot) noexcept {
    TASTY_SEAT_BODY(SaveImageSource);
    const auto b = find(slot);
    if (!b)
        return;
    b->exists = false;
    b->size = proto::FileSize{};

    b->path.clear();
    b->blank = svc::BlankSaveSpec{};
    b->manual = false;
    b->close_owed = false;
    b->attach = Attach::Detached;
}

SaveImageSource::Attach SaveImageSource::attach_state(proto::SlotIndex slot) const noexcept {
    const auto b = find(slot);
    return b ? b->attach : Attach::Detached;
}

void SaveImageSource::mark(proto::SlotIndex slot, Attach state) noexcept {
    TASTY_SEAT_BODY(SaveImageSource);
    if (const auto b = find(slot))
        b->attach = state;
}

infra::OptRef<svc::IStorageBackend> SaveImageSource::backend(proto::SlotIndex slot) noexcept {
    if (!slot.below<kSlots>())
        return {};
    return infra::OptRef<svc::IStorageBackend>{infra::at_index(backends_, slot)};
}

std::span<const std::byte> SaveImageSource::order(proto::SlotIndex slot) const noexcept {
    const auto b = find(slot);
    if (!b)
        return {};
    return infra::text_bytes(b->path.view());
}

bool SaveImageSource::fill_blank(
    proto::SlotIndex slot,
    proto::Lba lba,
    std::span<std::uint8_t> window
) {
    const auto b = find(slot);
    if (!b || !b->blank.declared())
        return false;
    svc::render_blank(b->blank, proto::byte_offset(lba, b->blank.lba_bytes), window);
    return true;
}

}
