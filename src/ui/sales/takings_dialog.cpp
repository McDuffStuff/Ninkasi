#include "ui/sales/takings_dialog.h"

#include "ui/date_widgets.h"
#include "ui/display.h"
#include "ui/search_combo.h"
#include "ui/widgets.h"

#include <imgui_stdlib.h>

#include <exception>

namespace ninkasi::ui {

namespace {

constexpr const char* kPopupId = "Taproom takings###takings_dialog";

} // namespace

TakingsDialog::TakingsDialog(app::SalesService& sales, float ui_scale) : sales_(sales), ui_scale_(ui_scale) {}

void TakingsDialog::open() {
    error_.clear();
    day_ = domain::today();
    channel_ = domain::Channel::Taproom;
    amount_ = 0.0;
    note_.clear();
    center_ = ImGui::GetMainViewport()->GetCenter();
    open_requested_ = true;
    is_open_ = true;
}

void TakingsDialog::close() {
    ImGui::CloseCurrentPopup();
    is_open_ = false;
}

void TakingsDialog::draw() {
    if (open_requested_) {
        ImGui::OpenPopup(kPopupId);
        open_requested_ = false;
    }
    if (!is_open_) return;
    ImGui::SetNextWindowPos(center_, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    bool keep_open = true;
    if (!ImGui::BeginPopupModal(kPopupId, &keep_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        is_open_ = false;
        return;
    }
    draw_form();
    ImGui::EndPopup();
}

void TakingsDialog::draw_form() {
    const float field = 220.0f * ui_scale_;
    if (ImGui::BeginTable("##takings", 2, ImGuiTableFlags_SizingFixedFit)) {
        table_row("Day");
        date_picker("##day", day_);
        table_row("Where");
        ImGui::SetNextItemWidth(field);
        enum_combo("##channel", channel_, domain::kChannels);
        table_row("Takings");
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        edit_money("##amount", amount_, sales_.currency(), 120.0f * ui_scale_);
        table_row("");
        ImGui::TextDisabled("Net of tax and tips: what the beer and everything else sold for.");
        table_row("Note");
        ImGui::SetNextItemWidth(field);
        ImGui::InputText("##note", &note_);
        ImGui::EndTable();
    }

    // Counted another way too, it would be counted twice.
    const domain::TaproomRevenue revenue = sales_.taproom_revenue();
    if (channel_ == domain::Channel::Taproom && revenue != domain::TaproomRevenue::Takings) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 460.0f * ui_scale_);
        ImGui::TextColored(kWarningColor, "Counted twice? The taproom's revenue comes from %s (Sales > Price list).",
                           domain::name(revenue).data());
        ImGui::PopTextWrapPos();
    }

    if (!error_.empty()) ImGui::TextColored(kErrorColor, "%s", error_.c_str());
    ImGui::Spacing();
    const bool enabled = amount_ > 0.0;
    ImGui::BeginDisabled(!enabled);
    bool save = ImGui::Button("Save");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || dialog_escape_pressed()) {
        close();
        return;
    }
    // Enter saves; a field being typed in has already taken its value.
    if (enabled && dialog_enter_pressed() && !ImGui::IsAnyItemActive()) save = true;
    if (enabled) {
        ImGui::SameLine();
        ImGui::TextDisabled("Enter saves");
    }
    if (!save) return;

    domain::Sale sale;
    sale.at = domain::from_local({day_.year, day_.month, day_.day, 23, 0});  // the end of the day
    sale.channel = channel_;
    sale.source = domain::SaleSource::Takings;
    sale.note = note_;
    domain::SaleLine takings;
    takings.kind = domain::LineKind::Other;
    takings.description = "Takings";
    takings.quantity = 1.0;
    takings.unit_price = amount_;
    sale.lines.push_back(takings);
    try {
        sales_.record_sale(sale, false);
        close();
    } catch (const std::exception& e) {
        error_ = e.what();
    }
}

} // namespace ninkasi::ui
