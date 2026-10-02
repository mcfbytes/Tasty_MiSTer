// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/file_bytes.h"

#include "app/mgl.h"
#include "cores/boot_asset.h"
#include "cores/config_slots.h"
#include "cores/file_tx.h"

namespace mister::app {

static_assert(FileBytes::kConfigSlots == cores::kMaxConfigSlots);
static_assert(FileBytes::kManifestId.v == FileBytes::kSlots - 1);
static_assert(FileBytes::config_id(cores::kMaxConfigSlots - 1).v < FileBytes::kManifestId.v);
static_assert(FileBytes::config_id(0).v > FileBytes::kRecycleSlots);

static_assert(FileBytes::kMaxLiveFiles == cores::kMaxStartAssets + (MglPlayer::kMaxItems - 1) +
                                              cores::kMaxBootBuffers + FileBytes::kHold - 1);
static_assert(FileBytes::kMaxLiveFiles <= FileBytes::kRecycleSlots);

static_assert(FileBytes::kPieceBytes % cores::kFileTxChunkBytes == 0);

void FileBytes::release_free_(std::uint32_t popped) noexcept {
    for (std::uint16_t id = 1; id <= kRecycleSlots; ++id) {
        if (free_(id, popped)) std::vector<std::uint8_t>{}.swap(storage_[id].bytes);
    }
}

std::uint16_t FileBytes::live_files_(std::uint32_t popped) const noexcept {
    std::uint16_t n = 0;
    for (std::uint16_t id = 1; id <= kRecycleSlots; ++id) {
        if (!free_(id, popped) && !storage_[id].bytes.empty()) ++n;
    }
    return n;
}

Ex<proto::FileId> FileBytes::intern(std::vector<std::uint8_t>&& bytes, std::string_view ext,
                                    std::string_view path, std::uint32_t load_addr,
                                    std::uint32_t at, std::uint32_t popped, std::uint32_t crc,
                                    std::uint64_t whole, std::uint64_t offset) {
    if (bytes.empty()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    release_free_(popped);
    const std::uint16_t id = next_;
    if (!free_(id, popped) || live_files_(popped) >= kMaxLiveFiles) {
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), id});
    }
    Slot& s = storage_[id];
    const std::uint64_t n = bytes.size();
    s.bytes = std::move(bytes);
    if (!s.ext.assign(ext) || !s.path.assign(path)) {
        s.bytes.clear();
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), id});
    }
    s.load_addr = load_addr;
    s.size_bytes = whole != 0 ? whole : n;
    s.offset = offset;
    s.crc = crc;
    s.save = {};
    named_at_[id] = at;
    live_[id] = true;
    next_ = static_cast<std::uint16_t>(id >= kRecycleSlots ? 1 : id + 1);
    return proto::FileId{id};
}

[[nodiscard]] Ex<proto::FileId> FileBytes::intern_path(std::string_view path,
                                                       std::uint64_t size_bytes, std::uint32_t at,
                                                       std::uint32_t popped) {
    if (path.empty()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    release_free_(popped);
    const std::uint16_t id = next_;
    if (!free_(id, popped)) {
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), id});
    }
    Slot& s = storage_[id];
    s.ext.clear();
    if (!s.path.assign(path)) {
        s.path.clear();
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), id});
    }
    s.load_addr = 0;
    s.size_bytes = size_bytes;
    s.crc = 0;
    s.save = {};
    named_at_[id] = at;
    live_[id] = true;
    next_ = static_cast<std::uint16_t>(id >= kRecycleSlots ? 1 : id + 1);
    return proto::FileId{id};
}

Ex<proto::FileId> FileBytes::intern_at(proto::FileId fid, std::vector<std::uint8_t>&& bytes,
                                       std::string_view ext, std::string_view path,
                                       std::uint32_t load_addr) {
    const std::uint16_t id = fid.v;
    if (id <= kRecycleSlots || id >= kSlots) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), id});
    }
    Slot& s = storage_[id];
    const std::uint64_t n = bytes.size();
    s.bytes = std::move(bytes);
    if (!ext.empty() && !s.ext.assign(ext)) {
        s.bytes.clear();
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), id});
    }
    if (!path.empty() && !s.path.assign(path)) {
        s.bytes.clear();
        s.ext.clear();
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), id});
    }
    if (ext.empty()) s.ext.clear();
    if (path.empty()) s.path.clear();
    s.load_addr = load_addr;
    s.size_bytes = n;
    s.crc = 0;
    s.save = {};
    return fid;
}

bool FileBytes::stamp_save(proto::FileId file, proto::FileId save) noexcept {
    const std::uint16_t id = file.v;
    if (id == kNone || id >= kSlots || storage_[id].bytes.empty()) return false;
    storage_[id].save = save;
    return true;
}

const FileBytes::Slot* FileBytes::get(proto::FileId fid) const noexcept {
    const std::uint16_t id = fid.v;
    if (id == kNone || id >= kSlots) return nullptr;
    if (storage_[id].bytes.empty() && storage_[id].path.empty()) return nullptr;
    return &storage_[id];
}

}  // namespace mister::app
