// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/ram_image.h"

#include <algorithm>

namespace mister::cores {
namespace {

class Pcg32 {
public:
    explicit Pcg32(std::uint32_t seed) noexcept {
        step_();
        state_ += seed;
        step_();
    }
    [[nodiscard]] std::uint64_t draw() noexcept {
        const std::uint64_t hi = step_();
        return hi << 32 | step_();
    }

private:
    std::uint32_t step_() noexcept {
        const std::uint64_t s = state_;
        state_ = s * 6364136223846793005ull + kIncrement;
        const auto xorshift = static_cast<std::uint32_t>((s >> 18 ^ s) >> 27);
        const auto rotate = static_cast<std::uint32_t>(s >> 59);
        return xorshift >> rotate | xorshift << (-rotate & 31u);
    }
    static constexpr std::uint64_t kIncrement = 1;
    std::uint64_t state_ = 0;
};

class Lfsr {
public:
    explicit Lfsr(std::uint32_t seed) noexcept : iter_(seed) {}
    [[nodiscard]] std::uint8_t draw() noexcept {
        iter_ = iter_ >> 1 ^ (((iter_ & 1u) - 1u) & 0xedb88320u);
        return static_cast<std::uint8_t>(iter_);
    }

private:
    std::uint32_t iter_;
};

std::vector<std::uint8_t> low_entropy_bytes(Pcg32& rng, std::uint32_t size) {
    std::vector<std::uint8_t> data(size);
    const auto lobit = static_cast<std::uint32_t>(rng.draw() & 3);
    const auto hibit = static_cast<std::uint32_t>((lobit + 8 + (rng.draw() & 3)) & 15);
    auto lovalue = static_cast<std::uint8_t>(rng.draw() & 255);
    auto hivalue = static_cast<std::uint8_t>(rng.draw() & 255);
    if ((rng.draw() & 3) == 0) lovalue = 0;
    if ((rng.draw() & 1) == 0) hivalue = static_cast<std::uint8_t>(~lovalue);
    for (std::uint32_t address = 0; address < size; ++address) {
        auto value = (address & 1u << lobit) != 0 ? lovalue : hivalue;
        if ((address & 1u << hibit) != 0) value = static_cast<std::uint8_t>(~value);
        if ((rng.draw() & 511) == 0) value ^= static_cast<std::uint8_t>(1u << (rng.draw() & 7));
        if ((rng.draw() & 2047) == 0) value ^= static_cast<std::uint8_t>(1u << (rng.draw() & 7));
        data[address] = value;
    }
    return data;
}

}  // namespace

std::vector<std::uint8_t> expand(const RamImageRecipe& r) {
    if (r.kind == RamImageRecipe::Kind::None) return {};
    std::vector<std::uint8_t> out(r.size(), r.second_fill);
    std::uint8_t* first = out.data();
    switch (r.kind) {
        case RamImageRecipe::Kind::Flat:
            std::fill_n(first, r.first_bytes, r.first_fill);
            break;
        case RamImageRecipe::Kind::Pcg32: {
            Pcg32 rng{r.seed};
            switch (r.entropy) {
                case RamImageRecipe::Entropy::None:
                    std::fill_n(first, r.first_bytes, std::uint8_t{0});
                    break;
                case RamImageRecipe::Entropy::Low:
                    std::ranges::copy(low_entropy_bytes(rng, r.first_bytes), first);
                    break;
                case RamImageRecipe::Entropy::High:
                    for (std::uint32_t i = 0; i < r.first_bytes; ++i)
                        first[i] = static_cast<std::uint8_t>(rng.draw());
                    break;
            }
            break;
        }
        case RamImageRecipe::Kind::Lfsr: {

            Lfsr rng{r.seed};
            for (std::uint8_t& b : out)
                b = rng.draw();
            if (r.second_bytes >= 0xF8u)
                std::fill_n(first + r.first_bytes + 0xF4u, 4, std::uint8_t{0});
            break;
        }
        case RamImageRecipe::Kind::None:
            break;
    }
    return out;
}

}  // namespace mister::cores
