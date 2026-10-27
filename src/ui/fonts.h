#pragma once
// The UI font.

namespace ninkasi::ui {

// Loads Roboto (embedded in the executable) as the UI font, sized for
// `scale`, the monitor's content scale (1.0 at 96 DPI). Call once, after
// ImGui::CreateContext() and before the first frame.
void load_fonts(float scale);

} // namespace ninkasi::ui
