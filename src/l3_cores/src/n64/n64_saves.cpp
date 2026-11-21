// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64/n64_saves.h"
#include "cores/save_row_count.h"

#include <algorithm>
#include <utility>

#include "infra/indexed_array.h"
#include "infra/path_parts.h"
#include "infra/prelude.h"
#include "svc/card_dirs.h"

namespace mister::cores::n64 {

SaveChannel::SaveChannel(infra::OptRef<svc::IStorageBackend> store) noexcept : store_port_(store) {}

Ex<void> SaveChannel::plan(
    const ILayoutKeySource& live,
    std::string_view image_path,
    std::string_view core_dir,
    std::span<const SavePartition> declared
) {
    clear();

    layout_key_ = live.layout_key(SaveRowCount::truncating(declared.size()));
    layout_set_ = true;

    if (declared.empty()) {
        mount_plan_.emplace();
        return {};
    }
    if (declared.size() > kMaxSaveRows) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(declared.size())}
        );
    }

    NvramPolicy policy{};
    policy.naming = SaveNaming::PerImagePartition;
    policy.directory = core_dir;
    auto loaded = NvramStore::load(policy, image_path, declared);
    if (!loaded)
        return std::unexpected(loaded.error());
    store_.emplace(std::move(*loaded));
    for (const SavePartition& p : store_->partitions()) {
        total_ += p.length;
    }

    SavePath& stock = plan_.stock;
    bool ok = stock.assign(svc::kSavesPrefix);
    ok = ok && stock.append(core_dir);
    ok = ok && stock.append("/");
    ok = ok && stock.append(infra::stem(image_path));
    ok = ok && stock.append(".sav");
    if (!ok)
        stock.clear();

    mount_plan_.emplace();
    const SaveRowCount n = SaveRowCount::truncating(
        std::min<std::size_t>(store_->partitions().size(), MountPlan::kMaxMounts)
    );
    for (SaveRow row{}; row != n.end(); row = row.next()) {
        mount_plan_->add(mount_at(row));
    }
    return {};
}

void SaveChannel::clear() noexcept {

    for (SaveOrder& o : orders_)
        o = SaveOrder{};
    plan_ = SavePlan{};
    outbox_ = SaveAttach::Planned{};
    store_.reset();
    layout_key_ = SaveLayoutKey{};
    layout_set_ = false;
    total_ = 0;
    mount_plan_.reset();
}

void SaveChannel::forget_plan() noexcept {
    mount_plan_.reset();
}

infra::OptRef<const ISaveChannel::MountPlan> SaveChannel::mount_plan() const noexcept {
    TASTY_SEAT_BODY(SaveChannel);
    if (!mount_plan_)
        return {};
    return infra::OptRef<const MountPlan>{*mount_plan_};
}

Ex<ISaveChannel::Mount> SaveChannel::mount_at(SaveRow row) {
    TASTY_SEAT_BODY(SaveChannel);
    const auto order = orders_.find(row);
    if (!store_.has_value()
        || !order
        || !SaveRowCount::truncating(
                std::min<std::size_t>(store_->partitions().size(), MountPlan::kMaxMounts)
        )
                .contains(row)) {
        return std::unexpected(row.refusal(Errc::slot_range, ERR_SITE()));
    }
    const SavePartition& p = infra::at_index(store_->partitions(), row);
    auto path = store_->path_for(p.name);
    if (!path) {
        ++name_refusals_;
        return std::unexpected(path.error());
    }
    SaveOrder& o = *order;
    o.path = *path;
    const CreateImage image = p.suffix == save_suffix(MemoryType::Cpak) ? CreateImage::ControllerPak
                                                                        : CreateImage::Blank;

    if (p.length > create_cap(row) || !SaveOrder::create_fits(image, proto::FileSize{p.length})) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), p.length});
    }

    o.offset = p.offset;
    o.bytes = p.length;
    o.create = image;
    o.row = row;
    plan_.rows[row] = SavePlan::Row{
        .length = proto::FileSize{p.length},
        .byteswap = needs_byteswap(p.suffix)
    };

    Mount m{};
    m.row = row;
    m.size = proto::FileSize{p.length};
    m.path = o.path.view();
    return m;
}

infra::OptRef<svc::IStorageBackend> SaveChannel::backend() noexcept {
    TASTY_SEAT_BODY(SaveChannel);

    return store_port_;
}

std::span<const std::byte> SaveChannel::compose_order(SaveRow row, OrderPart part) noexcept {
    TASTY_SEAT_BODY(SaveChannel);
    const auto order = orders_.find(row);
    if (!order)
        return {};
    if (part == OrderPart::RowOnly)
        return std::as_bytes(std::span(&*order, 1));
    outbox_.order = *order;
    outbox_.plan = plan_;
    return std::as_bytes(std::span(&outbox_, 1));
}

proto::SlotAttributes SaveChannel::attributes(proto::SlotIndex) const {
    TASTY_SEAT_BODY(SaveChannel);
    proto::SlotAttributes a{};
    a.writable = true;

    a.channel_bytes = channel_bytes();
    return a;
}

std::uint64_t SaveChannel::channel_bytes() const noexcept {
    if (!layout_set_ || !store_.has_value())
        return total_;
    const SaveLayout live = expand_save_layout(layout_key_);
    if (!live.valid)
        return total_;
    const RowExtent last = live.last().value_or(RowExtent{});
    return static_cast<std::uint64_t>(last.offset) + last.length;
}

proto::ImageTag SaveChannel::request_tag(proto::SlotIndex) const noexcept {
    TASTY_SEAT_BODY(SaveChannel);
    if (!layout_set_ || !store_.has_value())
        return {};
    return layout_key_.to_tag();
}

void SaveChannel::note_layout(const ILayoutKeySource& live) noexcept {
    TASTY_SEAT_BODY(SaveChannel);
    if (!store_.has_value())
        return;
    layout_key_ = live.layout_key(SaveRowCount::truncating(store_->partitions().size()));
    layout_set_ = true;
}

void SaveChannel::note_attached(proto::SlotIndex, bool exists, proto::FileSize) noexcept {
    TASTY_SEAT_BODY(SaveChannel);
    if (exists) {
        ++opens_;
    } else {
        ++open_failures_;
    }
}

}
