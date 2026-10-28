#pragma once
// The UI font.

struct ImFont;

namespace ninkasi::ui {

// Loads Roboto (embedded in the executable) as the UI font, sized for
// `scale`, the monitor's content scale (1.0 at 96 DPI), and a larger copy for
// headings. Call once, after ImGui::CreateContext() and before the first frame.
void load_fonts(float scale);

// The larger font, for ImGui::PushFont(); the UI font if there is none.
ImFont* heading_font();

} // namespace ninkasi::ui
