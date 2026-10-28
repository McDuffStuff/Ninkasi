#include "ui/fonts.h"

#include <imgui.h>

#include <cstddef>
#include <initializer_list>

// Generated at configure time by cmake/EmbedFile.cmake.
namespace ninkasi::embedded {
extern const unsigned char kRobotoMediumTtf[];
extern const std::size_t kRobotoMediumTtfSize;
} // namespace ninkasi::embedded

namespace ninkasi::ui {

namespace {

constexpr float kBaseFontSize = 16.0f;
constexpr float kHeadingFontSize = 26.0f;

// Latin-1 plus the punctuation and symbols the UI uses (° · – — “ ” … − ≈).
// Roboto has no arrows, so UI text must not use them.
const ImWchar kGlyphRanges[] = {
    0x0020, 0x00FF,  // Basic Latin and Latin-1 Supplement
    0x2010, 0x2027,  // dashes, quotation marks, bullet, ellipsis
    0x2212, 0x2212,  // minus sign
    0x2248, 0x2248,  // almost equal to
    0,
};

} // namespace

void load_fonts(float scale) {
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;  // the data lives in the executable
    // The first font added is the default.
    for (const float size : {kBaseFontSize, kHeadingFontSize}) {
        ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(embedded::kRobotoMediumTtf),
                                                   static_cast<int>(embedded::kRobotoMediumTtfSize), size * scale,
                                                   &config, kGlyphRanges);
    }
}

ImFont* heading_font() {
    ImFontAtlas* fonts = ImGui::GetIO().Fonts;
    return fonts->Fonts.Size > 1 ? fonts->Fonts[1] : fonts->Fonts[0];
}

} // namespace ninkasi::ui
