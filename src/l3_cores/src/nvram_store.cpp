// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/nvram_store.h"

namespace mister::cores {
namespace {

std::string_view base_of(std::string_view path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

std::string_view stem_of(std::string_view name) {
    const auto dot = name.rfind('.');
    return dot == std::string_view::npos ? name : name.substr(0, dot);
}

[[nodiscard]] bool append_index(SavePath& out, std::uint8_t v) {
    char d[3];
    std::size_t n = 0;
    do {
        d[n++] = static_cast<char>('0' + (v % 10));
        v = static_cast<std::uint8_t>(v / 10);
    } while (v != 0);
    bool ok = true;
    while (n-- != 0)
        ok = ok && out.append(std::string_view{&d[n], 1});
    return ok;
}

[[nodiscard]] Ex<SavePath> built(bool ok, const SavePath& out) {
    if (!ok) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(out.size())});
    }
    return out;
}

}  // namespace

Fnv1aHex fnv1a64_hex(std::string_view basename) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (char c : basename) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 0x100000001b3ull;
    }
    constexpr char kHex[] = "0123456789abcdef";
    char digits[16];
    for (int i = 15; i >= 0; --i) {
        digits[static_cast<std::size_t>(i)] = kHex[h & 0xF];
        h >>= 4;
    }
    Fnv1aHex out;
    (void)out.assign(std::string_view{digits, sizeof digits});
    return out;
}

Ex<NvramStore> NvramStore::load(NvramPolicy policy, std::string_view image_basename,
                                std::span<const SavePartition> declared) {
    if (declared.size() > kMaxPartitions) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(declared.size())});
    }
    NvramStore s;
    s.policy_ = policy;
    if (!s.image_.assign(base_of(image_basename))) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(image_basename.size())});
    }
    std::uint32_t offset = 0;
    for (std::size_t i = 0; i < declared.size(); ++i) {
        if (declared[i].length == 0 || declared[i].name.empty()) {
            return std::unexpected(
                Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(i)});
        }
        s.table_[i] = declared[i];
        s.table_[i].offset = offset;
        offset += declared[i].length;
    }
    s.count_ = static_cast<std::uint8_t>(declared.size());
    return s;
}

Ex<void> NvramStore::relayout(std::span<const std::uint32_t> lengths) {
    if (lengths.size() != count_) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(lengths.size())});
    }
    std::uint32_t offset = 0;
    for (std::uint8_t i = 0; i < count_; ++i) {
        if (lengths[i] == 0) {
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), i});
        }
        table_[i].length = lengths[i];
        table_[i].offset = offset;
        offset += lengths[i];
    }
    return {};
}

Ex<SavePartition> NvramStore::partition_view(std::string_view partition) const {
    for (std::uint8_t i = 0; i < count_; ++i) {
        if (table_[i].name == partition) return table_[i];
    }
    return std::unexpected(Error{Errc::not_found, ERR_SITE(), count_});
}

Ex<PartitionHit> NvramStore::resolve(std::uint64_t absolute_offset) const {
    for (std::uint8_t i = 0; i < count_; ++i) {
        const std::uint64_t end = static_cast<std::uint64_t>(table_[i].offset) + table_[i].length;
        if (absolute_offset < end) {
            return PartitionHit{i, absolute_offset - table_[i].offset};
        }
    }
    return std::unexpected(Error{Errc::not_found, ERR_SITE(), count_});
}

Ex<SavePath> NvramStore::path_for(std::string_view partition) const {
    auto p = partition_view(partition);
    if (!p) return std::unexpected(p.error());
    switch (policy_.naming) {
        case SaveNaming::PerDiscFnv1a:
            return per_disc_path(*p, false);
        case SaveNaming::PerCore:
            return per_core_path(*p, false);
        case SaveNaming::PerCoreIndexed:
            return per_core_path(*p, true);
        case SaveNaming::PerImagePartition:
            return per_image_path(*p);
    }
    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
}

Ex<SavePath> NvramStore::stock_path_for(std::string_view partition) const {
    auto p = partition_view(partition);
    if (!p) return std::unexpected(p.error());
    if (policy_.naming != SaveNaming::PerDiscFnv1a) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }
    return per_disc_path(*p, true);
}

Ex<SavePath> NvramStore::per_disc_path(const SavePartition&, bool stock) const {
    SavePath out;
    bool ok = out.assign(policy_.force_sd_root ? "/media/fat/saves/" : "saves/");
    ok = ok && out.append(policy_.directory);
    ok = ok && out.append("/");
    ok = ok && out.append(policy_.prefix);
    if (!stock) {
        ok = ok && out.append("-");
        ok = ok && out.append(fnv1a64_hex(image_.view()).view());
    }
    ok = ok && out.append(".");
    ok = ok && out.append(policy_.extension);
    return built(ok, out);
}

Ex<SavePath> NvramStore::per_core_path(const SavePartition& p, bool indexed) const {
    SavePath out;
    bool ok = out.assign(policy_.prefix);
    if (indexed && p.index != 0) ok = ok && append_index(out, p.index);
    ok = ok && out.append(".");
    ok = ok && out.append(policy_.extension);
    return built(ok, out);
}

Ex<SavePath> NvramStore::per_image_path(const SavePartition& p) const {
    SavePath out;
    bool ok = out.assign("saves/");
    ok = ok && out.append(policy_.directory);
    ok = ok && out.append("/");
    ok = ok && out.append(stem_of(image_.view()));
    if (p.index != 0) {
        ok = ok && out.append("_");
        ok = ok && append_index(out, p.index);
    }
    ok = ok && out.append(p.suffix);
    return built(ok, out);
}

SavePath pick_disc_path(const svc::Vfs& vfs, const SavePath& own, const SavePath& stock,
                        std::uint64_t length) {
    if (vfs.file_exists(own.view()) || !vfs.file_exists(stock.view())) return own;
    auto f = vfs.open(stock.view(), svc::OpenMode::Read);
    if (!f) return own;
    auto size = (*f)->size();
    if (!size || size->v != length) return own;
    return stock;
}

}  // namespace mister::cores
