// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64_saves.h"

namespace mister::cores::n64 {
namespace {

std::string_view basename_of(std::string_view path) noexcept {
    const auto slash = path.find_last_of('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

std::string_view stem_of(std::string_view name) noexcept {
    const auto dot = name.rfind('.');
    return dot == std::string_view::npos ? name : name.substr(0, dot);
}

}

SaveChannel::SaveChannel(const svc::Vfs& vfs) noexcept
    : io_(vfs),
      ports_{{io_, 0}, {io_, 1}, {io_, 2}, {io_, 3}, {io_, 4}} {}

static_assert(kMaxSaveRows == 5, "one port initializer per mount index");

Ex<void> SaveChannel::plan(
    const ISaveLayout& live,
    std::string_view image_path,
    std::string_view core_dir,
    std::span<const SavePartition> declared
) {
    clear();
    live_ = infra::OptRef<const ISaveLayout>{live};

    pending_plan_ = 0;
    if (declared.empty())
        return {};
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

    bool ok = stock_path_.assign("saves/");
    ok = ok && stock_path_.append(core_dir);
    ok = ok && stock_path_.append("/");
    ok = ok && stock_path_.append(stem_of(basename_of(image_path)));
    ok = ok && stock_path_.append(".sav");
    if (!ok)
        stock_path_.clear();

    pending_plan_ = store_->partitions().size();
    return {};
}

void SaveChannel::clear() noexcept {

    for (SaveOrder& o : orders_)
        o = SaveOrder{};
    for (std::uint32_t& d : declared_)
        d = 0;
    for (bool& s : byteswap_)
        s = false;
    for (auto& p : paths_)
        p.clear();
    stock_path_.clear();
    store_.reset();
    live_ = std::nullopt;
    total_ = 0;
    pending_plan_.reset();
}

std::optional<std::size_t> SaveChannel::claim_mount_plan() noexcept {
    TASTY_SEAT_BODY(SaveChannel);
    const auto n = pending_plan_;
    pending_plan_.reset();
    return n;
}

Ex<ISaveChannel::Mount> SaveChannel::mount_at(std::size_t i) {
    TASTY_SEAT_BODY(SaveChannel);
    if (!store_.has_value() || i >= kMaxSaveRows || i >= store_->partitions().size()) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(i)});
    }
    const SavePartition& p = store_->partitions()[i];
    auto path = store_->path_for(p.name);
    if (!path) {
        ++name_refusals_;
        return std::unexpected(path.error());
    }
    paths_[i] = *path;

    SaveOrder& b = orders_[i];
    b.path.clear();
    if (!b.path.assign(paths_[i].view())) {
        ++name_refusals_;
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(i)});
    }
    if (p.length == 0 || p.length > blank_cap(i)) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), p.length});
    }

    b.stock.clear();
    if (!b.stock.assign(stock_path_.view()))
        b.stock.clear();
    b.offset = p.offset;
    b.create = p.length;
    declared_[i] = p.length;
    byteswap_[i] = needs_byteswap(p.suffix);
    b.cpak = p.suffix == save_suffix(MemoryType::Cpak);

    Mount m{};
    m.slot = proto::IoIndex{static_cast<std::uint8_t>(i)};
    m.size = proto::FileSize{p.length};
    m.path = paths_[i].view();
    return m;
}

svc::IStorageBackend* SaveChannel::backend(std::size_t i) noexcept {
    TASTY_SEAT_BODY(SaveChannel);
    if (i >= kMaxSaveRows)
        return nullptr;
    return &ports_[i];
}

std::span<const std::byte> SaveChannel::order(std::size_t i) noexcept {
    TASTY_SEAT_BODY(SaveChannel);
    if (i >= kMaxSaveRows)
        return {};

    SaveOrder& o = orders_[i];
    for (std::size_t r = 0; r < kMaxSaveRows; ++r) {
        o.declared[r] = declared_[r];
        o.byteswap[r] = byteswap_[r];
    }
    return std::as_bytes(std::span(&o, 1));
}

proto::SlotAttributes SaveChannel::attributes(proto::SlotIndex) const {
    TASTY_SEAT_BODY(SaveChannel);
    proto::SlotAttributes a{};
    a.writable = true;

    a.channel_bytes = channel_bytes();
    return a;
}

std::uint64_t SaveChannel::channel_bytes() const noexcept {
    if (!live_ || !store_.has_value())
        return total_;
    const SaveLayout live = expand_save_layout(live_->layout_key(store_->partitions().size()));
    if (!live.valid)
        return total_;
    const std::size_t last = live.count - 1u;
    return static_cast<std::uint64_t>(live.offset[last]) + live.length[last];
}

std::uint8_t SaveChannel::request_tag(proto::SlotIndex) const noexcept {
    TASTY_SEAT_BODY(SaveChannel);
    if (!live_ || !store_.has_value())
        return 0;
    return live_->layout_key(store_->partitions().size());
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
