#pragma once
// Pictures: reading PNG, JPEG, BMP and GIF files, for the floor's background.
// Decoding is done by stb_image (public domain).

#include <optional>
#include <span>
#include <vector>

namespace ninkasi::app {

// Decoded pixels, 8-bit RGBA, row by row from the top.
struct Pixels {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
};

struct PictureSize {
    int width = 0;
    int height = 0;
};

// The picture's size in pixels, without decoding it; nullopt if `file`
// isn't a picture Ninkasi can read.
std::optional<PictureSize> picture_size(std::span<const unsigned char> file);

// The picture's pixels, or nullopt if it can't be read.
std::optional<Pixels> decode_picture(std::span<const unsigned char> file);

// Halves the picture, averaging each 2 × 2 block, until neither side is
// longer than `max_side`.
Pixels shrink_to_fit(Pixels pixels, int max_side);

} // namespace ninkasi::app
