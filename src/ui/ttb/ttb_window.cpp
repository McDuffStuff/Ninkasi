#include "ui/ttb/ttb_window.h"

#include "ui/display.h"
#include "ui/file_dialog.h"
#include "ui/widgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <fstream>

namespace ninkasi::ui {

namespace {

using domain::ReportColumn;

constexpr ReportColumn kColumns[] = {ReportColumn::Cellar,       ReportColumn::RackingBulk, ReportColumn::Kegs,
                                     ReportColumn::BottlingBulk, ReportColumn::Cases,       ReportColumn::Total};

// "12.50": barrels to two places.
std::string barrels_text(domain::Centibarrels amount) {
    return number_text(domain::to_barrels(amount), 2);
}

std::string dollars_text(double amount) {
    return "$" + number_text(amount, 2);
}

// Lines that sum others up, shown highlighted.
bool is_total_line(int line) {
    return line == 1 || line == 13 || line == 33 || line == 34;
}

} // namespace

TtbWindow::TtbWindow(app::TtbService& ttb, const app::BatchService& batches, const app::PackagedGoodsService& goods,
                     float ui_scale)
    : ttb_(ttb), batches_(batches), goods_(goods), ui_scale_(ui_scale) {
    // Open on the last full period: the one being filed.
    const domain::ReportCadence cadence = ttb_.cadence();
    period_ = domain::previous_period(domain::period_containing(domain::today(), cadence), cadence);
    brewery_number_ = ttb_.brewery_number();
}

void TtbWindow::show() {
    visible_ = true;
    focus_ = true;
}

void TtbWindow::refresh() {
    if (!stale_ && batch_revision_ == batches_.revision() && goods_revision_ == goods_.revision()) return;
    report_ = ttb_.report(period_);
    batch_revision_ = batches_.revision();
    goods_revision_ = goods_.revision();
    stale_ = false;
}

void TtbWindow::draw() {
    if (!visible_) return;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(1240.0f, 820.0f) * ui_scale_, ImGuiCond_FirstUseEver);
    const bool raise = focus_;
    if (focus_) {
        ImGui::SetNextWindowFocus();
        focus_ = false;
    }
    const bool expanded = ImGui::Begin("TTB reports", &visible_);
    if (raise) raise_os_window();
    if (expanded) {
        refresh();
        draw_toolbar();
        if (ImGui::BeginTabBar("##ttb_tabs")) {
            if (ImGui::BeginTabItem("Report of operations")) {
                draw_report();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Excise tax")) {
                draw_tax();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Settings")) {
                draw_settings();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void TtbWindow::draw_toolbar() {
    domain::ReportCadence cadence = ttb_.cadence();
    ImGui::SetNextItemWidth(130.0f * ui_scale_);
    if (enum_combo("##cadence", cadence, domain::kReportCadences)) {
        ttb_.set_cadence(cadence);
        period_ = domain::period_containing(period_.first, cadence);
        stale_ = true;
    }
    ImGui::SetItemTooltip("TTB F 5130.9 is filed monthly, or quarterly if the brewery owed no more than $50,000 in "
                          "beer tax last year and expects the same this year.");
    ImGui::SameLine();
    if (ImGui::ArrowButton("##previous", ImGuiDir_Left)) {
        period_ = domain::previous_period(period_, cadence);
        stale_ = true;
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(domain::period_label(period_, cadence).c_str());
    ImGui::SameLine();
    if (ImGui::ArrowButton("##next", ImGuiDir_Right)) {
        period_ = domain::next_period(period_, cadence);
        stale_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("This period")) {
        period_ = domain::period_containing(domain::today(), cadence);
        stale_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Export CSV...")) export_csv();
    if (!status_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", status_.c_str());
    }
    if (period_.last >= domain::today()) {
        ImGui::TextColored(kWarningColor, "This period isn't over yet: the report so far.");
    }
    ImGui::Spacing();
}

void TtbWindow::draw_report() {
    const domain::ReportOfOperations& report = report_.operations;
    ImGui::TextDisabled("TTB F 5130.9 Part 1 - Beer Summary, in barrels of 31 gallons");
    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                       ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit |
                                       ImGuiTableFlags_ScrollY | ImGuiTableFlags_PadOuterX;
    const float footer = ImGui::GetTextLineHeightWithSpacing() * 3.0f;
    if (!ImGui::BeginTable("##operations", 8, kFlags, ImVec2(0.0f, -footer))) return;
    ImGui::TableSetupScrollFreeze(0, 2);
    ImGui::TableSetupColumn("Line");
    ImGui::TableSetupColumn("Operations", ImGuiTableColumnFlags_WidthStretch);
    for (const char* column : {"(b)", "(c)", "(d)", "(e)", "(f)", "(g)"}) {
        ImGui::TableSetupColumn(column, ImGuiTableColumnFlags_WidthFixed, 82.0f * ui_scale_);
    }
    // Two header rows: the form's areas, then its columns.
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    const char* areas[] = {"", "", "Cellar", "Racking", "", "Bottling", "", "Totals"};
    for (int column = 0; column < 8; ++column) {
        ImGui::TableSetColumnIndex(column);
        ImGui::TableHeader(areas[column]);
    }
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    const char* names[] = {"Line", "Operations", "Bulk (b)", "Bulk (c)", "Keg (d)", "Bulk (e)", "Case (f)", "(g)"};
    for (int column = 0; column < 8; ++column) {
        ImGui::TableSetColumnIndex(column);
        ImGui::TableHeader(names[column]);
    }

    for (int line = 1; line <= domain::kReportLines; ++line) {
        if (line == 1 || line == 14) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s", line == 1 ? "Additions to beer inventory" : "Removals from beer inventory");
        }
        ImGui::TableNextRow();
        if (is_total_line(line)) {
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImGuiCol_TableHeaderBg));
        }
        ImGui::TableNextColumn();
        ImGui::Text("%d", line);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(domain::report_line_text(line).data());
        // The column may be too narrow for TTB's longer lines.
        if (!domain::report_line_text(line).empty()) ImGui::SetItemTooltip("%s", domain::report_line_text(line).data());
        for (ReportColumn column : kColumns) {
            ImGui::TableNextColumn();
            const domain::Centibarrels amount = report.at(line, column);
            // Show the figures; leave empty lines empty, except the stock and totals.
            if (amount != 0) {
                const std::string text = barrels_text(amount);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x -
                                     ImGui::CalcTextSize(text.c_str()).x);
                ImGui::TextUnformatted(text.c_str());
            } else if (is_total_line(line)) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x -
                                     ImGui::CalcTextSize("0.00").x);
                ImGui::TextDisabled("0.00");
            }
        }
    }
    ImGui::EndTable();

    // What TTB will ask about.
    if (report.at(30, ReportColumn::Total) != 0 || report.at(31, ReportColumn::Total) != 0) {
        ImGui::TextColored(kWarningColor, "Explain recorded losses (line 30) and shortages (line 31) in the report's "
                                          "remarks. TTB may tax unexplained shortages.");
    }
    if (report.at(13, ReportColumn::Total) == report.at(34, ReportColumn::Total)) {
        ImGui::TextDisabled("Line 13 equals line 34. Lines 35 and 36, adjustments to earlier reports, aren't "
                            "worked out here.");
    } else {
        ImGui::TextColored(kErrorColor, "Line 13 doesn't equal line 34.");
    }
}

void TtbWindow::draw_tax() {
    const domain::ExciseTax& tax = report_.tax;
    ImGui::TextDisabled("Federal excise tax on beer removed for consumption or sale (26 U.S.C. 5051)");
    ImGui::Spacing();
    if (ImGui::BeginTable("##tax", 2, ImGuiTableFlags_SizingFixedFit)) {
        table_row("Taxed removals");
        ImGui::Text("%s bbl", number_text(tax.barrels, 2).c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(report lines 14 and 15)");
        table_row("Earlier this year");
        ImGui::Text("%s bbl", number_text(report_.earlier_taxable_barrels, 2).c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(counts toward the year's first 60,000 barrels)");
        ImGui::EndTable();
    }
    ImGui::Spacing();
    if (ImGui::BeginTable("##bands", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX)) {
        ImGui::TableSetupColumn("Rate", ImGuiTableColumnFlags_WidthFixed, 220.0f * ui_scale_);
        ImGui::TableSetupColumn("Barrels", ImGuiTableColumnFlags_WidthFixed, 100.0f * ui_scale_);
        ImGui::TableSetupColumn("Tax", ImGuiTableColumnFlags_WidthFixed, 120.0f * ui_scale_);
        ImGui::TableHeadersRow();
        const auto band = [](const char* rate, double barrels, double per_barrel) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(rate);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(number_text(barrels, 2).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(dollars_text(barrels * per_barrel).c_str());
        };
        if (ttb_.small_brewer()) band("$3.50 (first 60,000 bbl)", tax.at_reduced, domain::kReducedRatePerBarrel);
        band("$16.00 (to 6,000,000 bbl)", tax.at_middle, domain::kMiddleRatePerBarrel);
        band("$18.00", tax.at_full, domain::kFullRatePerBarrel);
        ImGui::TableNextRow();
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImGuiCol_TableHeaderBg));
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Tax due");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(number_text(tax.barrels, 2).c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(dollars_text(tax.tax).c_str());
        ImGui::EndTable();
    }
    ImGui::Spacing();
    wrapped_note("Pay it with TTB F 5000.24, the Excise Tax Return: semi-monthly, or quarterly if the brewery "
                 "expects to owe no more than $50,000 for the year, or annually if no more than $1,000. Tax on "
                 "beer returned to the brewery (line 7) can be claimed back as an adjustment.");
}

void TtbWindow::draw_settings() {
    if (ImGui::BeginTable("##settings", 2, ImGuiTableFlags_SizingFixedFit)) {
        table_row("Brewery number");
        ImGui::SetNextItemWidth(200.0f * ui_scale_);
        ImGui::InputTextWithHint("##number", "BR-XX-12345", &brewery_number_);
        if (ImGui::IsItemDeactivatedAfterEdit()) ttb_.set_brewery_number(brewery_number_);
        table_row("Excise rate");
        bool small_brewer = ttb_.small_brewer();
        if (ImGui::Checkbox("Small brewer rate ($3.50 a barrel on the year's first 60,000)", &small_brewer)) {
            ttb_.set_small_brewer(small_brewer);
            stale_ = true;
        }
        table_row("");
        ImGui::TextDisabled("For domestic brewers producing no more than 2,000,000 barrels a year.");
        ImGui::EndTable();
    }
    ImGui::Spacing();
    wrapped_note("The report counts beer when it's knocked out into a fermenter as produced (line 2), packaging as "
                 "racked or bottled, and every way beer leaves packaged goods on its own line. Losses and dumps "
                 "in the cellar, and volume corrections, go on lines 30, 28, 11 and 31.");
}

void TtbWindow::export_csv() {
    // "TTB 5130.9 2026-Q4.csv", "TTB 5130.9 2026-11.csv", "TTB 5130.9 2026.csv".
    std::string name = "TTB 5130.9 " + std::to_string(period_.first.year);
    if (ttb_.cadence() == domain::ReportCadence::Quarterly) {
        name += "-Q" + std::to_string((period_.first.month - 1) / 3 + 1);
    } else if (ttb_.cadence() == domain::ReportCadence::Monthly) {
        name += (period_.first.month < 10 ? "-0" : "-") + std::to_string(period_.first.month);
    }
    name += ".csv";
    const std::optional<std::filesystem::path> path =
        save_file_dialog("Export TTB report", "CSV files", "*.csv", "csv", name.c_str());
    if (!path) return;
    std::ofstream file(*path, std::ios::binary);
    // A byte order mark, so spreadsheets read the file as UTF-8.
    file << "\xEF\xBB\xBF" << ttb_.csv(report_);
    const std::u8string where = path->filename().u8string();
    status_ = (file ? "Saved " : "Couldn't write ") + std::string(where.begin(), where.end());
}

} // namespace ninkasi::ui
