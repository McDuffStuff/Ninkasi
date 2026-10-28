#include "ui/texture.h"

#include <GLFW/glfw3.h>  // brings in the system's OpenGL header

#include <algorithm>
#include <utility>

// OpenGL 1.2; Windows' header stops at 1.1, but every driver has it.
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace ninkasi::ui {

namespace {
constexpr int kLargestSide = 4096;
}

Texture::Texture(app::Pixels pixels) {
    GLint card_limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &card_limit);
    pixels = app::shrink_to_fit(std::move(pixels), card_limit > 0 ? std::min(card_limit, kLargestSide) : kLargestSide);

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pixels.width, pixels.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 pixels.rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    handle_ = texture;
}

Texture::~Texture() {
    const GLuint texture = handle_;
    if (texture != 0) glDeleteTextures(1, &texture);
}

} // namespace ninkasi::ui
