// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/nvram_store.h"

#include "cores/fnv1a64.h"
#include "infra/path_parts.h"

namespace mister::cores {
namespace {

[[nodiscard]] Ex<SavePath> built(bool ok, const SavePath& out) {
    if (!ok) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(out.size())}
        );
    }
    return out;
}

}

Fnv1aHex fnv1a64_hex(std::string_view basename) {
    const auto digits = Fnv1a64::digest_of(basename).hex();
    Fnv1aHex out;
    (void)out.assign(std::string_view{digits.data(), digits.size()});
    return out;
}

Ex<NvramStore> NvramStore::load(
    NvramPolicy policy,
    std::string_view image_basename,
    std::span<const SavePartition> declared
) {
    if (declared.size() > kMaxPartitions) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(declared.size())}
        );
    }
    NvramStore s;
    s.policy_ = policy;
    if (!s.image_.assign(infra::basename(image_basename))) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(image_basename.size())}
        );
    }
    std::uint32_t offset = 0;
    for (std::size_t i = 0; i < declared.size(); ++i) {
        if (declared[i].length == 0 || declared[i].name.empty()) {
            return std::unexpected(
                Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(i)}
            );
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
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(lengths.size())}
        );
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
        if (table_[i].name == partition)
            return table_[i];
    }
    return std::unexpected(Error{Errc::not_found, ERR_SITE(), count_});
}

Ex<PartitionHit> NvramStore::resolve(std::uint64_t absolute_offset) const {
    for (std::uint8_t i = 0; i < count_; ++i) {
        const std::uint64_t end = static_cast<std::uint64_t>(table_[i].offset) + table_[i].length;
        if (absolute_offset < end) {
            return PartitionHit{SaveRow{i}, absolute_offset - table_[i].offset};
        }
    }
    return std::unexpected(Error{Errc::not_found, ERR_SITE(), count_});
}

Ex<SavePath> NvramStore::path_for(std::string_view partition) const {
    auto p = partition_view(partition);
    if (!p)
        return std::unexpected(p.error());
    switch (policy_.naming) {
        case SaveNaming::PerDiscFnv1a:
            return per_disc_path(*p);
        case SaveNaming::PerCore:
            return per_core_path(*p);
        case SaveNaming::PerCoreIndexed:
            return per_core_indexed_path(*p);
        case SaveNaming::PerImagePartition:
            return per_image_path(*p);
    }
    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
}

Ex<SavePath> NvramStore::stock_path_for(std::string_view partition) const {
    auto p = partition_view(partition);
    if (!p)
        return std::unexpected(p.error());
    if (policy_.naming != SaveNaming::PerDiscFnv1a) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }
    return stock_disc_path(*p);
}

Ex<SavePath> NvramStore::per_disc_path(const SavePartition&) const {
    SavePath out;
    bool ok = out.assign(policy_.force_sd_root ? "/media/fat/saves/" : "saves/");
    ok = ok && out.append(policy_.directory);
    ok = ok && out.append("/");
    ok = ok && out.append(policy_.prefix);
    ok = ok && out.append("-");
    ok = ok && out.append(fnv1a64_hex(image_.view()).view());
    ok = ok && out.append(".");
    ok = ok && out.append(policy_.extension);
    return built(ok, out);
}

Ex<SavePath> NvramStore::stock_disc_path(const SavePartition&) const {
    SavePath out;
    bool ok = out.assign(policy_.force_sd_root ? "/media/fat/saves/" : "saves/");
    ok = ok && out.append(policy_.directory);
    ok = ok && out.append("/");
    ok = ok && out.append(policy_.prefix);
    ok = ok && out.append(".");
    ok = ok && out.append(policy_.extension);
    return built(ok, out);
}

Ex<SavePath> NvramStore::per_core_path(const SavePartition&) const {
    SavePath out;
    bool ok = out.assign(policy_.prefix);
    ok = ok && out.append(".");
    ok = ok && out.append(policy_.extension);
    return built(ok, out);
}

Ex<SavePath> NvramStore::per_core_indexed_path(const SavePartition& p) const {
    SavePath out;
    bool ok = out.assign(policy_.prefix);
    if (p.digit)
        ok = ok && out.append(p.digit->text().view());
    ok = ok && out.append(".");
    ok = ok && out.append(policy_.extension);
    return built(ok, out);
}

Ex<SavePath> NvramStore::per_image_path(const SavePartition& p) const {
    SavePath out;
    bool ok = out.assign("saves/");
    ok = ok && out.append(policy_.directory);
    ok = ok && out.append("/");
    ok = ok && out.append(infra::stem(image_.view()));
    if (p.digit) {
        ok = ok && out.append("_");
        ok = ok && out.append(p.digit->text().view());
    }
    ok = ok && out.append(p.suffix);
    return built(ok, out);
}

}
