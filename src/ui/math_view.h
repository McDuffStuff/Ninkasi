#pragma once
// Typesets LaTeX math (the subset calc/latex.h reads) with ImGui: fractions
// stacked over a bar, raised powers, lowered subscripts, radicals, brackets
// that grow with what they hold, and cases. It draws the same tree the
// formula evaluator computes from, so what's shown is what's computed.

#include <imgui.h>

#include <string_view>

namespace ninkasi::ui {

// Draws `latex` at the cursor, the way ImGui::Text draws text, `size` pixels
// tall (0 for a little larger than the UI font). Returns false, drawing the
// source as plain text instead, if the LaTeX can't be read.
bool math_text(std::string_view latex, float size = 0.0f);

// Draws "result = latex".
bool math_equation(std::string_view result, std::string_view latex, float size = 0.0f);

// The space math_equation() takes.
ImVec2 math_equation_size(std::string_view result, std::string_view latex, float size = 0.0f);

} // namespace ninkasi::ui
