#include "app/picture.h"

#include <algorithm>
#include <climits>
#include <utility>

// Only the formats Ninkasi offers; the rest of stb_image stays out of the build.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <stb_image.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace ninkasi::app {

namespace {

// stb_image takes the length as an int.
bool fits(std::span<const unsigned char> file) {
    return !file.empty() && file.size() <= static_cast<std::size_t>(INT_MAX);
}

} // namespace

std::optional<PictureSize> picture_size(std::span<const unsigned char> file) {
    if (!fits(file)) return std::nullopt;
    int width = 0;
    int height = 0;
    int channels = 0;
    if (!stbi_info_from_memory(file.data(), static_cast<int>(file.size()), &width, &height, &channels)) {
        return std::nullopt;
    }
    if (width <= 0 || height <= 0) return std::nullopt;
    return PictureSize{width, height};
}

std::optional<Pixels> decode_picture(std::span<const unsigned char> file) {
    if (!fits(file)) return std::nullopt;
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load_from_memory(file.data(), static_cast<int>(file.size()), &width, &height,
                                                &channels, 4);
    if (!data) return std::nullopt;
    Pixels pixels;
    pixels.width = width;
    pixels.height = height;
    pixels.rgba.assign(data, data + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    stbi_image_free(data);
    return pixels;
}

Pixels shrink_to_fit(Pixels pixels, int max_side) {
    while ((pixels.width > max_side || pixels.height > max_side) && pixels.width > 1 && pixels.height > 1) {
        Pixels half;
        half.width = pixels.width / 2;
        half.height = pixels.height / 2;
        half.rgba.resize(static_cast<std::size_t>(half.width) * static_cast<std::size_t>(half.height) * 4);
        const auto at = [&](int x, int y, int channel) {
            return static_cast<unsigned>(
                pixels.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(pixels.width) +
                             static_cast<std::size_t>(x)) * 4 + static_cast<std::size_t>(channel)]);
        };
        for (int y = 0; y < half.height; ++y) {
            for (int x = 0; x < half.width; ++x) {
                for (int channel = 0; channel < 4; ++channel) {
                    const unsigned sum = at(2 * x, 2 * y, channel) + at(2 * x + 1, 2 * y, channel) +
                                         at(2 * x, 2 * y + 1, channel) + at(2 * x + 1, 2 * y + 1, channel);
                    half.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(half.width) +
                               static_cast<std::size_t>(x)) * 4 + static_cast<std::size_t>(channel)] =
                        static_cast<unsigned char>((sum + 2) / 4);
                }
            }
        }
        pixels = std::move(half);
    }
    return pixels;
}

} // namespace ninkasi::app
