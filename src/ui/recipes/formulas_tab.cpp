#include "ui/recipes/formulas_tab.h"

#include "domain/fuzzy_match.h"
#include "ui/display.h"
#include "ui/fonts.h"
#include "ui/math_view.h"
#include "ui/search_combo.h"
#include "ui/widgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <exception>
#include <utility>

namespace ninkasi::ui {

namespace {

constexpr const char* kResetAllPopup = "Put every formula back?";

// "result = latex", scrolling sideways if it's wider than the pane. Only the
// equation scrolls, so the text around it still wraps to the pane.
void equation(const char* id, std::string_view result, std::string_view latex) {
    const ImVec2 size = math_equation_size(result, latex);
    const bool wide = size.x > ImGui::GetContentRegionAvail().x;
    const float height = size.y + (wide ? ImGui::GetStyle().ScrollbarSize : 0.0f) + 2.0f;
    if (ImGui::BeginChild(id, ImVec2(0.0f, height), ImGuiChildFlags_None,
                          wide ? ImGuiWindowFlags_HorizontalScrollbar : ImGuiWindowFlags_None)) {
        math_equation(result, latex);
    }
    ImGui::EndChild();
}

std::string number_text(double value) {
    if (!std::isfinite(value)) return "--";
    char text[32];
    std::snprintf(text, sizeof text, "%.6g", value);
    return text;
}

// A short guide to the LaTeX the formulas use.
constexpr const char* kLatexHelp[][2] = {
    {"\\frac{a}{b}", "a over b"},
    {"a^{2}, e^{-0.04 t}", "powers"},
    {"T_{s}", "an input with a subscript, written as the formula's inputs are"},
    {"\\times  \\cdot  *", "multiply; things side by side multiply too"},
    {"\\sqrt{x}, \\sqrt[3]{x}", "roots"},
    {"\\ln x, \\log x, \\exp(x)", "logarithms and powers of e"},
    {"\\min(a, b), \\max(a, b)", "the smaller or larger"},
    {"\\left( ... \\right)", "brackets that grow to fit"},
    {"\\begin{cases} a & \\text{if } x < 1 \\\\ b & \\text{otherwise} \\end{cases}", "one value or another"},
    {"\\,", "a little space, for looks"},
};

} // namespace

FormulasTab::FormulasTab(app::FormulaService& formulas, float ui_scale) : formulas_(formulas), ui_scale_(ui_scale) {
    select(calc::Formula::SgToPlato);
}

void FormulasTab::select(calc::Formula formula) {
    selected_ = formula;
    editing_ = false;
    save_error_.clear();
    inputs_.clear();
    for (const calc::FormulaInput& input : calc::info(formula).inputs) inputs_.push_back(input.example);
}

void FormulasTab::check_draft() {
    compiled_draft_.reset();
    draft_problem_.clear();
    try {
        compiled_draft_ = calc::compile_formula(selected_, draft_);
    } catch (const calc::FormulaError& e) {
        draft_problem_ = e.what();
    }
}

void FormulasTab::draw() {
    ImGui::SetNextItemWidth(260.0f * ui_scale_);
    ImGui::InputTextWithHint("##search", "Find a formula", &search_);
    if (formulas_.can_adjust()) {
        ImGui::SameLine();
        ImGui::BeginDisabled(!formulas_.any_adjusted());
        if (ImGui::Button("Put every formula back...")) confirm_reset_all_ = true;
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Go back to Ninkasi's original formulas");
    }
    if (!status_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", status_.c_str());
    }
    for (const std::string& problem : formulas_.problems()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kWarningColor, "%s", problem.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();

    if (ImGui::BeginChild("##formula_list", ImVec2(280.0f * ui_scale_, 0.0f), ImGuiChildFlags_Borders)) draw_list();
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("##formula_details", ImVec2(0.0f, 0.0f))) draw_details();
    ImGui::EndChild();
    draw_reset_all_confirmation();
}

void FormulasTab::draw_list() {
    // By group; or, while searching, best match first.
    std::vector<const calc::FormulaInfo*> shown;
    std::vector<int> scores;
    for (const calc::FormulaInfo& formula : calc::formulas()) {
        const std::string searched = std::string(formula.name) + " " + std::string(formula.group);
        const std::optional<int> score = domain::fuzzy_score(searched, search_);
        if (!score) continue;
        shown.push_back(&formula);
        scores.push_back(*score);
    }
    if (!search_.empty()) {
        std::vector<std::size_t> order(shown.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return scores[a] > scores[b]; });
        std::vector<const calc::FormulaInfo*> sorted;
        for (std::size_t i : order) sorted.push_back(shown[i]);
        shown = std::move(sorted);
    }

    std::string_view group;
    for (const calc::FormulaInfo* found : shown) {
        const calc::FormulaInfo& formula = *found;
        if (search_.empty() && formula.group != group) {
            group = formula.group;
            ImGui::SeparatorText(group.data());
        }
        // A tag at the right: adjusted, or for reference only.
        const char* tag = formulas_.adjustment(formula.formula) ? "adjusted" : !formula.adjustable ? "reference" : "";
        const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        ImGui::PushID(static_cast<int>(formula.formula));
        if (ImGui::Selectable(formula.name.data(), selected_ == formula.formula)) select(formula.formula);
        if (*tag) {
            ImGui::SameLine(right - ImGui::CalcTextSize(tag).x);
            if (formulas_.adjustment(formula.formula)) {
                ImGui::TextColored(kWarningColor, "%s", tag);
            } else {
                ImGui::TextDisabled("%s", tag);
            }
        }
        ImGui::PopID();
    }
}

void FormulasTab::draw_details() {
    const calc::FormulaInfo& formula = calc::info(selected_);
    const std::optional<app::FormulaAdjustment> adjustment = formulas_.adjustment(selected_);

    ImGui::PushFont(heading_font());
    ImGui::TextUnformatted(formula.name.data(), formula.name.data() + formula.name.size());
    ImGui::PopFont();
    ImGui::TextDisabled("%s", formula.group.data());
    ImGui::Spacing();
    equation("##formula", formula.result, adjustment ? std::string_view(adjustment->latex) : formula.latex);
    if (adjustment) {
        ImGui::Spacing();
        ImGui::TextColored(kWarningColor, "Adjusted by %s, %s. The original:", adjustment->by.c_str(),
                           time_text(adjustment->at).c_str());
        equation("##original", formula.result, formula.latex);
    }
    ImGui::Spacing();
    if (!formula.note.empty()) wrapped_note(formula.note.data());

    ImGui::SeparatorText("Inputs");
    const int columns = formula.adjustable ? 3 : 2;
    if (ImGui::BeginTable("##inputs", columns, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("##symbol", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("##meaning", ImGuiTableColumnFlags_WidthStretch);
        if (formula.adjustable) ImGui::TableSetupColumn("Try it with", ImGuiTableColumnFlags_WidthFixed, 110.0f * ui_scale_);
        if (formula.adjustable) ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < formula.inputs.size(); ++i) {
            const calc::FormulaInput& input = formula.inputs[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            math_text(input.symbol);
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", input.meaning.data());
            if (formula.adjustable) {
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputDouble("##value", &inputs_[i], 0.0, 0.0, "%.6g");
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (!formula.adjustable) {
        ImGui::Spacing();
        wrapped_note("Shown for reference: it can't be adjusted itself, but the formulas it's built from can.");
        return;
    }
    draw_try_it(formula);

    if (!formulas_.can_adjust()) return;
    ImGui::SeparatorText("Adjust");
    if (editing_) {
        draw_editor(formula);
        return;
    }
    if (ImGui::Button("Adjust this formula...")) {
        editing_ = true;
        draft_ = adjustment ? adjustment->latex : std::string(formula.latex);
        save_error_.clear();
        check_draft();
    }
    if (adjustment) {
        ImGui::SameLine();
        if (ImGui::Button("Put back the original")) {
            try {
                formulas_.reset(selected_);
                status_ = std::string(formula.name) + " is back to the original.";
            } catch (const std::exception& e) {
                status_ = e.what();
            }
        }
    }
}

void FormulasTab::draw_try_it(const calc::FormulaInfo& formula) {
    // The formula in use now, the original if that's different, and the draft being written.
    std::string results;
    try {
        const double original = calc::compile_formula(selected_, formula.latex).evaluate(inputs_);
        if (const calc::Expression* adjusted = calc::adjusted_formula(selected_)) {
            results = "Gives " + number_text(adjusted->evaluate(inputs_)) + "    (the original gives " +
                      number_text(original) + ")";
        } else {
            results = "Gives " + number_text(original);
        }
    } catch (const calc::FormulaError& e) {
        results = e.what();
    }
    if (editing_ && compiled_draft_) results += "    With your changes: " + number_text(compiled_draft_->evaluate(inputs_));
    ImGui::Spacing();
    ImGui::TextUnformatted(results.c_str());
}

void FormulasTab::draw_editor(const calc::FormulaInfo& formula) {
    wrapped_note("Write the right side of the = in LaTeX, using the inputs as they're written above. Your formula "
                 "replaces the original everywhere Ninkasi uses it, and the original can always be put back.");
    if (ImGui::InputTextMultiline("##latex", &draft_, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4.5f))) {
        check_draft();
    }
    if (!draft_problem_.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kErrorColor, "%s", draft_problem_.c_str());
        ImGui::PopTextWrapPos();
    } else {
        ImGui::TextDisabled("It will read:");
        equation("##draft", formula.result, draft_);
    }
    if (!save_error_.empty()) ImGui::TextColored(kErrorColor, "%s", save_error_.c_str());

    ImGui::Spacing();
    const std::optional<app::FormulaAdjustment> adjustment = formulas_.adjustment(selected_);
    const std::string_view current = adjustment ? std::string_view(adjustment->latex) : formula.latex;
    ImGui::BeginDisabled(!compiled_draft_ || draft_ == current);
    if (ImGui::Button("Save")) {
        try {
            formulas_.adjust(selected_, draft_);
            status_ = formulas_.adjustment(selected_) ? std::string(formula.name) + " is adjusted."
                                                       : std::string(formula.name) + " is back to the original.";
            editing_ = false;
        } catch (const std::exception& e) {
            save_error_ = e.what();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) editing_ = false;
    if (draft_ != formula.latex) {
        ImGui::SameLine();
        if (ImGui::Button("Start from the original")) {
            draft_ = std::string(formula.latex);
            check_draft();
        }
    }

    if (ImGui::TreeNode("LaTeX help")) {
        if (ImGui::BeginTable("##help", 2, ImGuiTableFlags_SizingStretchProp)) {
            for (const auto& [example, meaning] : kLatexHelp) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(example);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", meaning);
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }
}

void FormulasTab::draw_reset_all_confirmation() {
    if (confirm_reset_all_) {
        ImGui::OpenPopup(kResetAllPopup);
        confirm_reset_all_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(kResetAllPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 400.0f * ui_scale_);
    ImGui::TextUnformatted("Every adjusted formula goes back to Ninkasi's original, everywhere it's used. The "
                           "adjustments are deleted.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button("Put them all back")) {
        try {
            formulas_.reset_all();
            status_ = "Every formula is back to the original.";
        } catch (const std::exception& e) {
            status_ = e.what();
        }
        editing_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || dialog_escape_pressed()) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

} // namespace ninkasi::ui
