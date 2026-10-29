#include "ui/sales/customers_window.h"

#include "domain/fuzzy_match.h"
#include "ui/display.h"
#include "ui/widgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <cfloat>
#include <exception>
#include <optional>

namespace ninkasi::ui {

namespace {

constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;

// True when the field just drawn was finished after an edit.
bool finished() {
    return ImGui::IsItemDeactivatedAfterEdit();
}

} // namespace

CustomersWindow::CustomersWindow(app::SalesService& sales, float ui_scale) : sales_(sales), ui_scale_(ui_scale) {}

void CustomersWindow::show() {
    visible_ = true;
    focus_ = true;
    reload();
}

void CustomersWindow::reload() {
    customers_ = sales_.customers();
    revision_ = sales_.revision();
}

void CustomersWindow::save(const domain::Customer& customer) {
    try {
        sales_.update_customer(customer);
        error_.clear();
    } catch (const std::exception& e) {
        // Rejected (a blank name): say why, and put the saved values back.
        error_ = e.what();
        revision_ = ~0ull;
    }
}

void CustomersWindow::draw() {
    if (!visible_) return;
    if (revision_ != sales_.revision()) reload();
    ImGui::SetNextWindowSize(ImVec2(860.0f, 480.0f) * ui_scale_, ImGuiCond_FirstUseEver);
    const bool raise = focus_;
    if (focus_) {
        ImGui::SetNextWindowFocus();
        focus_ = false;
    }
    const bool expanded = ImGui::Begin("Customers", &visible_);
    if (raise) raise_os_window();
    if (!expanded) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Add customer")) {
        try {
            name_to_focus_ = sales_.add_customer({0, "New customer", domain::Channel::Distributor, "", "", true});
            filter_.clear();  // so the new row shows
            error_.clear();
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f * ui_scale_);
    ImGui::InputTextWithHint("##filter", "Search", &filter_);
    ImGui::SameLine();
    ImGui::Checkbox("Show former customers", &show_former_);
    if (!error_.empty()) ImGui::TextColored(kErrorColor, "%s", error_.c_str());

    if (ImGui::BeginTable("##customers", 5, kTableFlags, ImVec2(0.0f, -FLT_MIN))) {
        const float s = ui_scale_;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Channel", ImGuiTableColumnFlags_WidthFixed, 140 * s);
        ImGui::TableSetupColumn("Contact", ImGuiTableColumnFlags_WidthFixed, 200 * s);
        ImGui::TableSetupColumn("Notes", ImGuiTableColumnFlags_WidthFixed, 200 * s);
        ImGui::TableSetupColumn("Buying", ImGuiTableColumnFlags_WidthFixed, 55 * s);
        ImGui::TableHeadersRow();

        for (domain::Customer& customer : customers_) {
            if (!customer.active && !show_former_) continue;
            if (!filter_.empty() && !domain::fuzzy_score(customer.name + " " + customer.contact, filter_)) continue;
            bool changed = false;
            ImGui::PushID(static_cast<int>(customer.id));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (customer.id == name_to_focus_) {
                // Straight into typing the new customer's name, over the placeholder.
                ImGui::SetKeyboardFocusHere();
                name_to_focus_ = 0;
            }
            ImGui::InputText("##name", &customer.name);
            changed |= finished();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= enum_combo("##channel", customer.channel, domain::kChannels);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##contact", "Phone, email, address", &customer.contact);
            changed |= finished();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##notes", &customer.notes);
            changed |= finished();
            ImGui::TableNextColumn();
            changed |= ImGui::Checkbox("##active", &customer.active);
            ImGui::SetItemTooltip("Clear it for a customer you no longer sell to: their sales are kept.");
            ImGui::PopID();
            if (changed) save(customer);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

} // namespace ninkasi::ui
