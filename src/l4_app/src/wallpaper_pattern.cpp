// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/wallpaper_pattern.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "app/hd_theme.h"
#include "app/types.h"
#include "infra/byte_view.h"
#include "infra/endian.h"
#include "infra/seat.h"
#include "svc/asset_name.h"
#include "svc/entropy.h"
#include "svc/read_bounded.h"
#include "svc/scan_filter.h"
#include "svc/vfs.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#endif
#ifdef TASTY_HAVE_HDOSD
#include <slint.h>
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace mister::app {
namespace {

namespace svc = ::mister::svc;

constexpr std::string_view kMenuPng = "menu.png";
constexpr std::string_view kMenuJpg = "menu.jpg";
constexpr std::string_view kMainLabel = "Main";
constexpr std::string_view kWallpapersDir = "wallpapers";

constexpr std::string_view kWallpaperExtensions = "PNGJPG";

struct WallpaperCap {
    std::uint64_t v = 0;

    [[nodiscard]] Ex<std::vector<std::byte>> read(
        const svc::Vfs& vfs,
        std::string_view path
    ) const {
        return svc::read_bounded(vfs, path, v);
    }
};

inline constexpr WallpaperCap kWallpaperCap{64u * 1024u * 1024u};

struct Decoded {
    OutputPixels w{};
    OutputPixels h{};
    std::vector<Rgb888> pixels{};
};

std::optional<Decoded> decode_wallpaper(std::span<const std::byte> bytes) {
#ifdef TASTY_HAVE_HDOSD
    const auto raw = infra::byte_cast<const std::uint8_t>(bytes);
    const slint::Image image = slint::Image::load_from_data(raw);
    const auto pixels = image.to_rgba8();
    if (!pixels)
        return std::nullopt;
    Decoded out;
    out.w = OutputPixels::of(pixels->width());
    out.h = OutputPixels::of(pixels->height());
    if (out.w.empty() || out.h.empty())
        return std::nullopt;
    out.pixels.resize(out.w.count<std::size_t>() * out.h.count<std::size_t>());
    const slint::Rgba8Pixel* src = pixels->begin();

    for (std::size_t i = 0; i < out.pixels.size(); ++i) {
        out.pixels[i] = rgb_from_channels(
            ColorChannel::of(src[i].r),
            ColorChannel::of(src[i].g),
            ColorChannel::of(src[i].b)
        );
    }
    return out;
#else
    (void)bytes;
    return std::nullopt;
#endif
}

std::string wallpapers_dir(const svc::Vfs& vfs, std::string_view ini_label) {
    std::string plain{kWallpapersDir};
    if (ini_label.empty() || ini_label == kMainLabel)
        return plain;
    std::string alt = plain + std::string{ini_label};
    return vfs.dir_exists(alt) ? alt : plain;
}

struct PickDraw {
    std::uint32_t v = 0;

    [[nodiscard]] static PickDraw drawn() noexcept {
        std::array<std::uint8_t, sizeof(std::uint32_t)> raw{};
        if (!svc::read_entropy(raw))
            return {};
        return PickDraw{infra::load_le<std::uint32_t>(std::span(raw))};
    }

    [[nodiscard]] const svc::DirEntry& among(
        std::span<const svc::DirEntry> entries
    ) const noexcept {
        return entries.size() == 1 ? entries.front() : entries[v % entries.size()];
    }
};

std::optional<std::string> image_in(const svc::Vfs& vfs, std::string_view dir, PickDraw pick) {
    const svc::ScanFilter filter{
        .extensions = kWallpaperExtensions,
        .directories = false,
        .files = true,
        .zip_as_directory = false
    };
    const auto entries = vfs.scan(dir, filter);
    if (!entries || entries->empty())
        return std::nullopt;
    return std::string{dir} + "/" + pick.among(*entries).name;
}

struct BlendWeight {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr ColorChannel mix(ColorChannel a, ColorChannel b) const noexcept {
        constexpr std::uint32_t kFull = 256;
        constexpr unsigned kShift = 8;
        return ColorChannel::of((a.scaled_by(kFull - v) + b.scaled_by(v)) >> kShift);
    }

    [[nodiscard]] constexpr Rgb888 mix(Rgb888 a, Rgb888 b) const noexcept {
        const auto [ar, ag, ab] = a.channels();
        const auto [br, bg, bb] = b.channels();
        return rgb_from_channels(
            mix(ColorChannel::of(ar), ColorChannel::of(br)),
            mix(ColorChannel::of(ag), ColorChannel::of(bg)),
            mix(ColorChannel::of(ab), ColorChannel::of(bb))
        );
    }
};

struct AxisPos {
    std::uint64_t v = 0;

    [[nodiscard]] static AxisPos of(std::int32_t at, OutputPixels dst, OutputPixels src) noexcept {
        constexpr std::uint64_t kOne = std::uint64_t{1} << 16;
        constexpr std::uint64_t kCentreHalf = 2;
        if (dst.empty() || src.empty())
            return {};
        const std::uint64_t centre = src.centre_sample_q16<std::uint64_t>(at, dst);
        const std::uint64_t back = kOne / kCentreHalf;
        const std::uint64_t last = src.last_cell_q16<std::uint64_t>();
        const std::uint64_t pos = centre > back ? centre - back : std::uint64_t{};
        return AxisPos{pos < last ? pos : last};
    }

    [[nodiscard]] constexpr std::size_t whole_cell() const noexcept {
        constexpr unsigned kShift = 16;
        return static_cast<std::size_t>(v >> kShift);
    }

    [[nodiscard]] constexpr std::size_t next_cell(OutputPixels src) const noexcept {
        const std::size_t after = whole_cell() + 1;
        return src.covers(after) ? after : src.last_index<std::size_t>();
    }

    [[nodiscard]] constexpr BlendWeight weight() const noexcept {
        constexpr unsigned kShift = 8;
        constexpr std::uint64_t kMask = 0xFF;
        return BlendWeight{static_cast<std::uint32_t>((v >> kShift) & kMask)};
    }

    [[nodiscard]] constexpr std::size_t row_start(OutputPixels stride) const noexcept {
        return stride.row_start_index(whole_cell());
    }

    [[nodiscard]] constexpr std::size_t next_row_start(
        OutputPixels stride,
        OutputPixels src
    ) const noexcept {
        return stride.row_start_index(next_cell(src));
    }
};

}

void WallpaperPattern::clear_() noexcept {
    path_.clear();
    colors_.clear();
    w_ = OutputPixels{};
    h_ = OutputPixels{};
}

void WallpaperPattern::load(const svc::Vfs& vfs, std::string_view ini_label) {
    TASTY_SEAT_BODY(WallpaperPattern);
    clear_();
    std::optional<std::string> found;
    if (vfs.file_exists(kMenuPng)) {
        found = std::string{kMenuPng};
    } else if (vfs.file_exists(kMenuJpg)) {
        found = std::string{kMenuJpg};
    } else {
        found = image_in(vfs, wallpapers_dir(vfs, ini_label), PickDraw::drawn());
    }
    if (!found)
        return;
    path_ = *found;
    const auto bytes = kWallpaperCap.read(vfs, path_);
    if (!bytes)
        return;
    decode_(*bytes);
}

void WallpaperPattern::decode_(std::span<const std::byte> bytes) {
    std::optional<Decoded> image = decode_wallpaper(bytes);
    if (!image)
        return;
    colors_ = std::move(image->pixels);
    w_ = image->w;
    h_ = image->h;
}

void WallpaperPattern::paint(PxPlane& canvas, HdRect box) const noexcept {
    TASTY_SEAT_BODY(WallpaperPattern);
    if (!has_image()) {
        fallback_.paint(canvas, box);
        return;
    }
    const HdRect on = box.intersect(canvas.bounds());
    if (on.is_empty())
        return;
    const std::optional<PxPlane> plane = canvas.sub(on);
    if (!plane)
        return;
    const OutputPixels dst_w = OutputPixels::of(box.w);
    const OutputPixels dst_h = OutputPixels::of(box.h);
    const std::span<const Rgb888> image{colors_};
    for (std::int32_t y = 0; y < on.h; ++y) {
        auto row = plane->row(PlaneRow{y});
        auto px = row.begin();
        const AxisPos sy = AxisPos::of(on.y + y - box.y, dst_h, h_);
        const std::size_t top_row = sy.row_start(w_);
        const std::size_t bottom_row = sy.next_row_start(w_, h_);
        const BlendWeight fy = sy.weight();
        for (std::int32_t x = 0; x < on.w; ++x, ++px) {
            const AxisPos sx = AxisPos::of(on.x + x - box.x, dst_w, w_);
            const std::size_t left = top_row + sx.whole_cell();
            const std::size_t right = top_row + sx.next_cell(w_);
            const BlendWeight fx = sx.weight();
            const Rgb888 upper = fx.mix(image[left], image[right]);
            const Rgb888 lower = fx.mix(
                image[bottom_row + sx.whole_cell()],
                image[bottom_row + sx.next_cell(w_)]
            );
            *px = hd_pack565(fy.mix(upper, lower));
        }
    }
}

}
