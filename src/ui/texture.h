#pragma once
// A picture on the graphics card, for ImGui to draw (ImDrawList::AddImage).
// Create and destroy it while the window's OpenGL context is current, as it
// is throughout the frame loop.

#include "app/picture.h"

#include <imgui.h>

namespace ninkasi::ui {

class Texture {
public:
    // Uploads `pixels`, halving them first if they're bigger than the
    // graphics card takes (or 4096 pixels a side, plenty for a floor plan).
    explicit Texture(app::Pixels pixels);
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    ImTextureID id() const { return static_cast<ImTextureID>(handle_); }

private:
    unsigned int handle_ = 0;
};

} // namespace ninkasi::ui
