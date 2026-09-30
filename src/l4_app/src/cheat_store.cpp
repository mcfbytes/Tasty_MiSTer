// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/cheat_store.h"

#include <algorithm>
#include <cstring>

#include "svc/vfs.h"

namespace mister::app {

static_assert(cores::kCheatTableBytes == 2048, "req / req: CHEAT_SIZE is 128 * 16 bytes total");
static_assert(cores::CheatGeometry{}.unit == 16 && cores::CheatGeometry{}.max_active == 128,
              "req: 16-byte units, 128 active codes, by default");
static_assert(cores::clamp_cheat_geometry(0, 0).unit == 16,
              "req: a non-positive unit falls back to 16");
static_assert(cores::clamp_cheat_geometry(32, 500).max_active == 64,
              "req: max_active is clamped so max * unit <= CHEAT_SIZE");
static_assert(sizeof(CheatBlob{}.bytes) == cores::kCheatTableBytes,
              "req: the crossing carries the WHOLE table, never a window");
static_assert(TxDigest::kPathCap == 1024, "req: stock's own cheat path buffer is char[1024]");
static_assert(CheatRecord::kNameCap == 256, "req: cheat_rec_t::name is char[256]");

namespace {

bool name_less(const CheatRecord& a, const CheatRecord& b) noexcept {
    const std::string_view x = a.name.view();
    const std::string_view y = b.name.view();
    const std::size_t n = x.size() < y.size() ? x.size() : y.size();
    for (std::size_t i = 0; i < n; ++i) {
        const auto cx = static_cast<unsigned char>(x[i]);
        const auto cy = static_cast<unsigned char>(y[i]);
        const int lx = cx >= 'A' && cx <= 'Z' ? cx + ('a' - 'A') : cx;
        const int ly = cy >= 'A' && cy <= 'Z' ? cy + ('a' - 'A') : cy;
        if (lx != ly) return lx < ly;
    }
    return x.size() < y.size();
}

std::string compose_dir(std::string_view core, const char* suffix) {
    std::string d{"cheats/"};
    d.append(core);
    if (suffix != nullptr) d.append(suffix);
    return d;
}

}  // namespace

void CheatStore::reset_() noexcept {
    TASTY_SEAT_BODY(CheatStore);
    recs_.clear();
    archive_.clear();
    geom_ = cores::CheatGeometry{};
    loaded_ = 0;
    blob_ = CheatBlob{};
}

void CheatStore::close() noexcept {
    TASTY_SEAT_BODY(CheatStore);
    reset_();
}

void CheatStore::sort_() noexcept {
    TASTY_SEAT_BODY(CheatStore);
    std::sort(recs_.begin(), recs_.end(), name_less);
}

Ex<void> CheatStore::open(const TxDigest& d, std::string_view core,
                          const cores::CheatLookup& look) {
    TASTY_SEAT_BODY(CheatStore);
    reset_();
    if (d.kind == TxDigest::Kind::None || core.empty()) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }

    const std::string core_dir = compose_dir(core, nullptr);
    const bool cd = d.kind == TxDigest::Kind::Mount && look.cd_asset_layout;
    const std::string cd_dir = cd ? compose_dir(core, look.cd_dir_suffix) : std::string{};

    svc::Vfs::AssetQuery q{};
    q.rom_path = d.path.view();
    q.crc = svc::Crc32{d.crc};
    q.ext = ".zip";
    q.core_dir = core_dir;
    q.cd_asset_dir = cd_dir;
    q.require_zip = true;
    auto found = vfs_->find_game_asset(q);
    if (!found) return std::unexpected(found.error());

    svc::ScanFilter f{};
    f.directories = false;
    f.files = true;
    f.zip_as_directory = true;
    auto members = vfs_->scan(*found, f);
    if (!members) {
        return std::unexpected(members.error());
    }

    archive_ = *found;
    for (const svc::DirEntry& e : *members) {
        if (e.is_dir) continue;
        CheatRecord r{};
        (void)r.name.assign(e.name);
        recs_.push_back(std::move(r));
    }
    sort_();
    ++opens_;
    return {};
}

void CheatStore::take(const CheatCatalog& cat) {
    TASTY_SEAT_BODY(CheatStore);
    reset_();
    geom_ = cores::clamp_cheat_geometry(static_cast<std::int32_t>(cat.unit),
                                        static_cast<std::int32_t>(cat.max_active));
    const std::size_t rows =
        cat.count <= CheatCatalog::kMaxRows ? cat.count : CheatCatalog::kMaxRows;
    for (std::size_t i = 0; i < rows; ++i) {
        const CheatCatalog::Row& src = cat.rows[i];
        const std::size_t end = static_cast<std::size_t>(src.offset) + src.len;
        if (end > CheatCatalog::kArenaBytes) {
            ++refusals_;
            continue;
        }

        if (geom_.unit == 0 || src.len % geom_.unit != 0) {
            ++refusals_;
            continue;
        }
        CheatRecord r{};
        (void)r.name.assign(std::string_view{src.name});
        r.bytes.assign(cat.data + src.offset, cat.data + end);
        r.loaded = true;
        recs_.push_back(std::move(r));
    }
    ++opens_;
}

CheatRow CheatStore::row(std::size_t i) const noexcept {
    TASTY_SEAT_BODY(CheatStore);
    if (i >= recs_.size()) return CheatRow{};
    return CheatRow{recs_[i].name.view(), recs_[i].enabled};
}

std::span<const std::uint8_t> CheatStore::blob() const noexcept {
    TASTY_SEAT_BODY(CheatStore);
    return {blob_.bytes, blob_.len};
}

void CheatStore::publish_() noexcept {
    TASTY_SEAT_BODY(CheatStore);
    std::size_t pos = 0;
    for (CheatRecord& r : recs_) {
        if (!r.enabled) continue;

        if (!r.loaded) {
            r.enabled = false;
            ++refusals_;
            continue;
        }

        if (pos + r.bytes.size() > sizeof(blob_.bytes)) {
            ++refusals_;
            continue;
        }
        std::memcpy(blob_.bytes + pos, r.bytes.data(), r.bytes.size());
        pos += r.bytes.size();
    }
    blob_.len = static_cast<std::uint16_t>(pos);
    blob_.unit = geom_.unit;
    loaded_ = geom_.unit != 0 ? pos / geom_.unit : 0;
    blob_cell_.publish(blob_);
}

Ex<bool> CheatStore::toggle(std::size_t i) {
    TASTY_SEAT_BODY(CheatStore);
    if (i >= recs_.size()) {
        ++refusals_;
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), 0});
    }
    CheatRecord& r = recs_[i];
    bool changed = false;

    if (r.enabled) {
        r.enabled = false;
        changed = true;
    } else {
        if (!r.loaded) {
            if (archive_.empty()) {
                ++refusals_;
            } else {
                std::string member = archive_;
                member.push_back('/');
                member.append(r.name.view());
                auto f = vfs_->open(member, svc::OpenMode::ReadWhole);
                if (!f) {
                    ++refusals_;
                } else if (auto sz = (*f)->size(); !sz) {
                    ++refusals_;
                } else {
                    const std::uint64_t len = sz->v;

                    if (len == 0 || geom_.unit == 0 || len % geom_.unit != 0) {
                        ++refusals_;
                    } else if (len / geom_.unit + loaded_ > geom_.max_active) {
                        ++refusals_;
                    } else {
                        std::vector<std::uint8_t> buf(static_cast<std::size_t>(len));
                        auto got = (*f)->read_at(0, std::as_writable_bytes(std::span{buf}));
                        if (!got || *got != buf.size()) {
                            ++refusals_;
                        } else {
                            r.bytes = std::move(buf);
                            r.loaded = true;
                        }
                    }
                }
            }
        }

        if (r.loaded) {
            r.enabled = true;
            changed = true;
        }
    }

    if (changed) publish_();
    return changed;
}

}  // namespace mister::app
