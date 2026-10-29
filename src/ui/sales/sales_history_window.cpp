#include "ui/sales/sales_history_window.h"

#include "domain/fuzzy_match.h"
#include "domain/money.h"
#include "ui/display.h"
#include "ui/sales/sales_widgets.h"
#include "ui/search_combo.h"
#include "ui/widgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <exception>

namespace ninkasi::ui {

namespace {

constexpr ImGuiTableFlags kListFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                       ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX |
                                       ImGuiTableFlags_ScrollY;

constexpr const char* kDeletePopup = "Delete this sale?###delete_sale";

// Who it was sold to: the customer, or the channel for a sale over the counter.
std::string sold_to(const domain::Sale& sale) {
    return sale.customer.empty() ? std::string(domain::name(sale.channel)) : sale.customer;
}

} // namespace

SalesHistoryWindow::SalesHistoryWindow(app::SalesService& sales, SaleDialog& sale_dialog,
                                       const calc::UnitPreferences& units, float ui_scale)
    : sales_(sales), sale_dialog_(sale_dialog), units_(units), ui_scale_(ui_scale) {}

void SalesHistoryWindow::show() {
    visible_ = true;
    focus_ = true;
    message_.clear();
    error_.clear();
    revision_ = ~0ull;
}

void SalesHistoryWindow::reload() {
    sales_list_ = sales_.sales();
    revision_ = sales_.revision();
}

bool SalesHistoryWindow::shown(const domain::Sale& sale) const {
    if (unpriced_only_ && !sale.needs_price()) return false;
    if (filter_.empty()) return true;
    return domain::fuzzy_score(sold_to(sale) + " " + sale.number + " " + sale_text(sale, units_), filter_)
        .has_value();
}

void SalesHistoryWindow::draw() {
    if (!visible_) return;
    if (revision_ != sales_.revision()) reload();
    ImGui::SetNextWindowSize(ImVec2(1000.0f, 600.0f) * ui_scale_, ImGuiCond_FirstUseEver);
    const bool raise = focus_;
    if (focus_) {
        ImGui::SetNextWindowFocus();
        focus_ = false;
    }
    const bool expanded = ImGui::Begin("Sales history", &visible_);
    if (raise) raise_os_window();
    if (!expanded) {
        ImGui::End();
        return;
    }
    const bool admin = sales_.can_set_prices();
    draw_toolbar(admin);
    draw_list(admin);
    draw_delete_confirmation();
    ImGui::End();
}

void SalesHistoryWindow::draw_toolbar(bool admin) {
    if (ImGui::Button("Record a sale...")) sale_dialog_.open();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f * ui_scale_);
    ImGui::InputTextWithHint("##filter", "Search", &filter_);
    if (!admin) return;

    // Lines without a price: shown on their own, and priced from the price list.
    const auto unpriced = std::count_if(sales_list_.begin(), sales_list_.end(),
                                        [](const domain::Sale& sale) { return sale.needs_price(); });
    ImGui::SameLine();
    ImGui::Checkbox("Only sales without prices", &unpriced_only_);
    ImGui::SameLine();
    ImGui::BeginDisabled(unpriced == 0);
    if (ImGui::Button("Price them from the price list")) {
        try {
            const app::SalesService::Pricing pricing = sales_.price_from_list();
            message_ = "Priced " + std::to_string(pricing.priced) + (pricing.priced == 1 ? " line." : " lines.");
            if (pricing.unpriced > 0) {
                message_ += " " + std::to_string(pricing.unpriced) +
                            (pricing.unpriced == 1 ? " line still has" : " lines still have") +
                            " no price on the price list.";
            }
            error_.clear();
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Each line without a price gets the price list's price on the day it was sold");
    if (unpriced > 0) {
        ImGui::SameLine();
        ImGui::TextColored(kWarningColor, "%d %s no price yet", static_cast<int>(unpriced),
                           unpriced == 1 ? "sale has a line with" : "sales have lines with");
    }
    if (!message_.empty()) ImGui::TextDisabled("%s", message_.c_str());
}

void SalesHistoryWindow::draw_list(bool admin) {
    if (!error_.empty()) ImGui::TextColored(kErrorColor, "%s", error_.c_str());
    if (sales_list_.empty()) {
        wrapped_note("No sales yet. Record one with Record a sale, or take beer out of packaged goods as sold.");
        return;
    }
    domain::Sale* selected = nullptr;
    for (domain::Sale& sale : sales_list_) {
        if (selected_ && sale.id == *selected_) selected = &sale;
    }
    const float list_height = selected ? ImGui::GetContentRegionAvail().y * 0.55f : 0.0f;
    const domain::Currency& currency = sales_.currency();
    const float s = ui_scale_;
    if (ImGui::BeginChild("##sales", ImVec2(0.0f, list_height)) &&
        ImGui::BeginTable("##sales_list", admin ? 6 : 5, kListFlags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("When", ImGuiTableColumnFlags_WidthFixed, 140 * s);
        ImGui::TableSetupColumn("Sold to", ImGuiTableColumnFlags_WidthFixed, 200 * s);
        ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch);
        if (admin) ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed, 110 * s);
        ImGui::TableSetupColumn("Number", ImGuiTableColumnFlags_WidthFixed, 90 * s);
        ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthFixed, 110 * s);
        ImGui::TableHeadersRow();
        for (const domain::Sale& sale : sales_list_) {
            if (!shown(sale)) continue;
            ImGui::PushID(static_cast<int>(sale.id));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(time_text(sale.at).c_str(), selected_ == sale.id,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                selected_ = sale.id;
                error_.clear();
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(sold_to(sale).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(sale_text(sale, units_).c_str());
            if (admin) {
                ImGui::TableNextColumn();
                if (sale.needs_price()) {
                    ImGui::TextColored(kWarningColor, "No price");
                } else {
                    ImGui::TextUnformatted(domain::format_money(sale.revenue(), currency).c_str());
                }
                // Deposits are paid back, so aren't in the total.
                if (sale.deposits() != 0.0) {
                    ImGui::SetItemTooltip("%s %s", sale.deposits() > 0.0 ? "And keg deposits of" : "Keg deposits refunded:",
                                          domain::format_money(std::abs(sale.deposits()), currency).c_str());
                }
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(sale.number.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", domain::name(sale.source).data());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    if (selected) draw_details(*selected, admin);
}

void SalesHistoryWindow::draw_details(domain::Sale& sale, bool admin) {
    const domain::Currency& currency = sales_.currency();
    ImGui::Separator();
    ImGui::Text("%s · %s", sold_to(sale).c_str(), time_text(sale.at).c_str());
    ImGui::SameLine();
    std::string about(domain::name(sale.channel));
    if (!sale.number.empty()) about += " · " + sale.number;
    about += " · recorded by " + sale.user;
    ImGui::TextDisabled("%s", about.c_str());
    if (!sale.note.empty()) wrapped_note(sale.note.c_str());

    if (admin && domain::can_delete(sale.source)) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete sale...")) confirm_delete_ = true;
        ImGui::SetItemTooltip("For a sale recorded by mistake: its beer goes back into packaged goods");
    }

    const float s = ui_scale_;
    if (!ImGui::BeginTable("##lines", admin ? 3 : 1, kListFlags)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch);
    if (admin) {
        ImGui::TableSetupColumn("Price each", ImGuiTableColumnFlags_WidthFixed, 140 * s);
        ImGui::TableSetupColumn("Amount", ImGuiTableColumnFlags_WidthFixed, 110 * s);
    }
    ImGui::TableHeadersRow();
    for (domain::SaleLine& line : sale.lines) {
        ImGui::PushID(static_cast<int>(line.id));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(line_text(line, units_).c_str());
        if (admin) {
            // A price typed here is saved when the field is left.
            ImGui::TableNextColumn();
            double price = line.unit_price.value_or(0.0);
            if (edit_money("##price", price, currency, 90.0f * s)) line.unit_price = price;
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                try {
                    sales_.set_line_price(line.id, line.unit_price);
                    error_.clear();
                } catch (const std::exception& e) {
                    error_ = e.what();
                    revision_ = ~0ull;  // put the saved price back
                }
            }
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            if (line.unit_price) {
                ImGui::TextUnformatted(domain::format_money(line.amount(), currency).c_str());
            } else {
                ImGui::TextColored(kWarningColor, "No price");
            }
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
    if (!admin) return;
    ImGui::Text("Total %s", domain::format_money(sale.revenue(), currency).c_str());
    if (sale.deposits() != 0.0) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s %s", sale.deposits() > 0.0 ? "and deposits of" : "and deposits refunded,",
                            domain::format_money(std::abs(sale.deposits()), currency).c_str());
    }
}

void SalesHistoryWindow::draw_delete_confirmation() {
    if (confirm_delete_) {
        ImGui::OpenPopup(kDeletePopup);
        confirm_delete_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(kDeletePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted("The sale is deleted, and its beer goes back into packaged goods.");
    ImGui::Spacing();
    if (ImGui::Button("Delete sale") && selected_) {
        try {
            sales_.delete_sale(*selected_);
            selected_.reset();
            error_.clear();
        } catch (const std::exception& e) {
            error_ = e.what();
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || dialog_escape_pressed()) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

} // namespace ninkasi::ui
