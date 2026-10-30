#include "ui/sales/pos_import_window.h"

#include "app/csv.h"
#include "domain/money.h"
#include "ui/display.h"
#include "ui/file_dialog.h"
#include "ui/inventory/inventory_views.h"
#include "ui/sales/sales_widgets.h"
#include "ui/search_combo.h"
#include "ui/widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <exception>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>

namespace ninkasi::ui {

namespace {

constexpr ImGuiTableFlags kListFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                       ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;

constexpr app::DateOrder kDateOrders[] = {app::DateOrder::YearMonthDay, app::DateOrder::MonthDayYear,
                                          app::DateOrder::DayMonthYear};
constexpr const char* kDateOrderNames[] = {"Year-month-day", "Month/day/year", "Day/month/year"};

void label_cell(const char* label) {
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
}

bool finished() {
    return ImGui::IsItemDeactivatedAfterEdit();
}

// "3", or "2.5".
std::string quantity_text(double count) {
    return number_text(count, count == static_cast<double>(static_cast<long long>(count)) ? 0 : 2);
}

// The first of `beers` named in an item ("Red Ale Pint" is Red Ale), the longest name first.
std::string beer_in(const std::string& item, std::vector<std::string> beers) {
    std::sort(beers.begin(), beers.end(), [](const auto& a, const auto& b) { return a.size() > b.size(); });
    std::string lower_item = item;
    for (char& c : lower_item) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const std::string& beer : beers) {
        std::string lower_beer = beer;
        for (char& c : lower_beer) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!lower_beer.empty() && lower_item.find(lower_beer) != std::string::npos) return beer;
    }
    return {};
}

// The pack an item's name gives: "4-pack" or "6 pk" is that many, "case" 24, otherwise 1.
int pack_in(const std::string& item) {
    std::string text = item;
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char* word : {"pack", "pk"}) {
        std::size_t at = text.find(word);
        if (at == std::string::npos) continue;
        while (at > 0 && (text[at - 1] == '-' || text[at - 1] == ' ')) --at;
        std::size_t start = at;
        while (start > 0 && std::isdigit(static_cast<unsigned char>(text[start - 1]))) --start;
        if (start < at) return std::max(1, std::stoi(text.substr(start, at - start)));
    }
    return text.find("case") != std::string::npos ? 24 : 1;
}

} // namespace

PosImportWindow::PosImportWindow(app::SalesService& sales, app::RecipeService& recipes, app::BatchService& batches,
                                 const app::InventoryService& inventory, const calc::UnitPreferences& units,
                                 float ui_scale)
    : sales_(sales), recipes_(recipes), batches_(batches), inventory_(inventory), units_(units), ui_scale_(ui_scale) {}

void PosImportWindow::show() {
    visible_ = true;
    focus_ = true;
    beers_ = beer_names(recipes_, batches_);
    can_sizes_ = can_sizes(inventory_);
    take_cans_ = sales_.pos_takes_cans();
    error_.clear();
    result_.clear();
}

void PosImportWindow::choose_file() {
    const std::vector<std::filesystem::path> files =
        open_files_dialog("Import point-of-sale sales", "CSV files", "*.csv *.txt");
    if (files.empty()) return;
    error_.clear();
    result_.clear();
    try {
        const std::filesystem::path& path = files.front();
        std::ifstream file(path, std::ios::binary);
        if (!file) throw std::runtime_error("Ninkasi couldn't read that file.");
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::vector<std::vector<std::string>> table = app::parse_csv(text, app::guess_csv_separator(text));
        if (table.size() < 2) throw std::runtime_error("That file has no rows of sales.");
        const std::u8string name = path.filename().u8string();
        file_name_ = std::string(name.begin(), name.end());
        table_ = std::move(table);
        signature_ = app::header_signature(table_.front());
        // The columns as last imported from files like it, or a guess.
        columns_ = sales_.pos_columns(signature_).value_or(app::guess_pos_columns(table_));
        replace_ = false;
        read_rows();
    } catch (const std::exception& e) {
        error_ = e.what();
    }
}

void PosImportWindow::read_rows() {
    rows_ = app::read_pos_rows(table_, columns_);
    items_.clear();
    for (const domain::PosRow& row : rows_.rows) {
        auto found = std::find_if(items_.begin(), items_.end(),
                                  [&](const ItemTotal& item) { return domain::same_beer(item.item, row.item); });
        if (found == items_.end()) {
            ItemTotal item;
            item.item = row.item;
            if (const domain::PosItem* mapping = domain::find_pos_item(sales_.pos_items(), row.item)) {
                item.mapping = *mapping;
                item.mapped = true;
            } else {
                item.mapping.item = row.item;
            }
            found = items_.insert(items_.end(), std::move(item));
        }
        found->quantity += row.quantity;
        found->amount += row.amount;
    }
    days_imported_ = sales_.pos_days_imported(rows_.rows);
}

void PosImportWindow::save(ItemTotal& item) {
    try {
        sales_.save_pos_item(item.mapping);
        item.mapped = true;
        error_.clear();
    } catch (const std::exception& e) {
        error_ = e.what();
    }
}

std::string PosImportWindow::missing(const ItemTotal& item) {
    const domain::PosItem& mapping = item.mapping;
    if (!item.mapped) return "Say what it is";
    switch (mapping.kind) {
    case domain::PosItemKind::Pour:
        if (mapping.beer.empty()) return "Choose the beer";
        if (!(mapping.pour_l > 0.0)) return "Give the pour's size";
        return {};
    case domain::PosItemKind::Package:
        if (mapping.beer.empty()) return "Choose the beer";
        if (!(mapping.unit_volume_l > 0.0)) return "Choose the size of can";
        return {};
    default:
        return {};
    }
}

void PosImportWindow::draw() {
    if (!visible_) return;
    ImGui::SetNextWindowSize(ImVec2(1000.0f, 760.0f) * ui_scale_, ImGuiCond_FirstUseEver);
    const bool raise = focus_;
    if (focus_) {
        ImGui::SetNextWindowFocus();
        focus_ = false;
    }
    const bool expanded = ImGui::Begin("Import POS sales", &visible_);
    if (raise) raise_os_window();
    if (!expanded) {
        ImGui::End();
        return;
    }
    draw_file();
    if (!table_.empty()) {
        draw_columns();
        if (columns_.complete() && !rows_.rows.empty()) {
            draw_items();
            draw_import();
        }
    }
    ImGui::End();
}

void PosImportWindow::draw_file() {
    wrapped_note("A CSV file exported from the taproom's point of sale, with a row for each item sold (or each "
                 "item's total for a day).");
    if (ImGui::Button("Choose file...")) choose_file();
    if (!file_name_.empty()) {
        ImGui::SameLine();
        ImGui::TextUnformatted(file_name_.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%zu rows", table_.size() - 1);
    }
    if (!error_.empty()) ImGui::TextColored(kErrorColor, "%s", error_.c_str());
}

bool PosImportWindow::column_combo(const char* label, int& column, const char* none) {
    const std::vector<std::string>& header = table_.front();
    const auto name = [&](int i) {
        const std::string& text = header[static_cast<std::size_t>(i)];
        return text.empty() ? "Column " + std::to_string(i + 1) : text;
    };
    const bool valid = column >= 0 && static_cast<std::size_t>(column) < header.size();
    const std::string shown = valid ? name(column) : (none ? none : "Choose the column");
    if (!begin_search_combo(label, shown.c_str())) return false;
    std::vector<std::string> names;
    std::vector<int> columns;
    if (none) {
        names.emplace_back(none);
        columns.push_back(-1);
    }
    for (std::size_t i = 0; i < header.size(); ++i) {
        names.push_back(name(static_cast<int>(i)));
        columns.push_back(static_cast<int>(i));
    }
    bool changed = false;
    for (std::size_t i : search_order(names)) {
        ImGui::PushID(static_cast<int>(i));
        if (search_item(names[i].c_str(), columns[i] == column) && columns[i] != column) {
            column = columns[i];
            changed = true;
        }
        ImGui::PopID();
    }
    end_search_combo();
    return changed;
}

void PosImportWindow::draw_columns() {
    ImGui::SeparatorText("Columns");
    const float field = 220.0f * ui_scale_;
    bool changed = false;
    if (ImGui::BeginTable("##columns", 4, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableNextRow();
        label_cell("Date");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(field);
        changed |= column_combo("##date", columns_.date, nullptr);
        label_cell("Dates are");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(field);
        const auto order = static_cast<std::size_t>(columns_.dates);
        if (begin_search_combo("##dates", kDateOrderNames[order])) {
            const std::vector<std::string> names(std::begin(kDateOrderNames), std::end(kDateOrderNames));
            for (std::size_t i : search_order(names)) {
                if (search_item(names[i].c_str(), i == order) && i != order) {
                    columns_.dates = kDateOrders[i];
                    changed = true;
                }
            }
            end_search_combo();
        }
        ImGui::SetItemTooltip("Dates with the year first are read that way whatever this says");
        ImGui::TableNextRow();
        label_cell("Item");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(field);
        changed |= column_combo("##item", columns_.item, nullptr);
        label_cell("Quantity");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(field);
        changed |= column_combo("##quantity", columns_.quantity, "Each row is one");
        ImGui::TableNextRow();
        label_cell("Net amount");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(field);
        changed |= column_combo("##amount", columns_.amount, nullptr);
        ImGui::SetItemTooltip("What each row took in, after discounts, before tax and tips");
        ImGui::EndTable();
    }
    if (changed) read_rows();
    if (!columns_.complete()) {
        wrapped_note("Choose the columns holding the date, the item and the amount.");
        return;
    }

    // The first rows as Ninkasi reads them, to check the columns by.
    const bool admin = sales_.can_set_prices();
    if (ImGui::BeginTable("##preview", admin ? 4 : 3, kListFlags)) {
        const float s = ui_scale_;
        ImGui::TableSetupColumn("Day", ImGuiTableColumnFlags_WidthFixed, 110 * s);
        ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthFixed, 320 * s);
        ImGui::TableSetupColumn("Quantity", ImGuiTableColumnFlags_WidthFixed, 80 * s);
        if (admin) ImGui::TableSetupColumn("Amount", ImGuiTableColumnFlags_WidthFixed, 110 * s);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < rows_.rows.size() && i < 6; ++i) {
            const domain::PosRow& row = rows_.rows[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(date_text(row.day).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.item.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(quantity_text(row.quantity).c_str());
            if (admin) {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(domain::format_money(row.amount, sales_.currency()).c_str());
            }
        }
        ImGui::EndTable();
    }
    if (rows_.rows.empty()) {
        ImGui::TextColored(kWarningColor, "No rows can be read with these columns.");
    } else if (rows_.rows.size() > 6) {
        ImGui::TextDisabled("and %zu more", rows_.rows.size() - 6);
    }
    if (rows_.skipped > 0) {
        ImGui::TextDisabled("%d %s left out: no item, or a date, quantity or amount that can't be read (totals, notes...)",
                            rows_.skipped, rows_.skipped == 1 ? "row" : "rows");
    }
}

void PosImportWindow::draw_items() {
    ImGui::SeparatorText("Items");
    wrapped_note("Say what each item is. It's remembered for the next import.");
    const bool admin = sales_.can_set_prices();
    const calc::Unit small = fill_volume_unit(units_);
    const float s = ui_scale_;
    if (!ImGui::BeginTable("##items", admin ? 7 : 6, kListFlags)) return;
    ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthFixed, 240 * s);
    ImGui::TableSetupColumn("Sold", ImGuiTableColumnFlags_WidthFixed, 60 * s);
    if (admin) ImGui::TableSetupColumn("Amount", ImGuiTableColumnFlags_WidthFixed, 100 * s);
    ImGui::TableSetupColumn("It is", ImGuiTableColumnFlags_WidthFixed, 160 * s);
    ImGui::TableSetupColumn("Beer", ImGuiTableColumnFlags_WidthFixed, 200 * s);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 130 * s);
    ImGui::TableSetupColumn("In a pack", ImGuiTableColumnFlags_WidthFixed, 90 * s);
    ImGui::TableHeadersRow();
    for (std::size_t i = 0; i < items_.size(); ++i) {
        ItemTotal& item = items_[i];
        domain::PosItem& mapping = item.mapping;
        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        const std::string still = missing(item);
        if (still.empty()) {
            ImGui::TextUnformatted(item.item.c_str());
        } else {
            ImGui::TextColored(kWarningColor, "%s", item.item.c_str());
            ImGui::SetItemTooltip("%s", still.c_str());
        }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(quantity_text(item.quantity).c_str());
        if (admin) {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(domain::format_money(item.amount, sales_.currency()).c_str());
        }

        // What it is; a beer's name in the item is a good first guess at which.
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        const char* shown = item.mapped ? domain::name(mapping.kind).data() : "Choose...";
        if (begin_search_combo("##kind", shown)) {
            std::vector<std::string> names;
            for (domain::PosItemKind kind : domain::kPosItemKinds) names.emplace_back(domain::name(kind));
            for (std::size_t k : search_order(names)) {
                const domain::PosItemKind kind = domain::kPosItemKinds[k];
                if (!search_item(names[k].c_str(), item.mapped && kind == mapping.kind)) continue;
                mapping.kind = kind;
                if (mapping.beer.empty()) mapping.beer = beer_in(item.item, beers_);
                if (kind == domain::PosItemKind::Pour && !(mapping.pour_l > 0.0)) mapping.pour_l = default_pour_l(units_);
                if (kind == domain::PosItemKind::Package && !(mapping.unit_volume_l > 0.0) && !can_sizes_.empty()) {
                    mapping.unit_volume_l = can_sizes_.front();
                }
                if (kind == domain::PosItemKind::Package && mapping.pack_size == 1) mapping.pack_size = pack_in(item.item);
                save(item);
            }
            end_search_combo();
        }

        const bool beer = item.mapped && (mapping.kind == domain::PosItemKind::Pour ||
                                          mapping.kind == domain::PosItemKind::Package);
        ImGui::TableNextColumn();
        if (beer) {
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (beer_combo("##beer", mapping.beer, beers_, nullptr)) save(item);
        }
        ImGui::TableNextColumn();
        if (beer && mapping.kind == domain::PosItemKind::Pour) {
            ImGui::SetNextItemWidth(70.0f * s);
            edit_quantity("##pour", mapping.pour_l, small, false);
            if (finished()) save(item);
            ImGui::SameLine();
            ImGui::TextUnformatted(calc::symbol(small).data());
        } else if (beer) {
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (can_size_combo("##can", mapping.unit_volume_l, can_sizes_, small, nullptr)) save(item);
        }
        ImGui::TableNextColumn();
        if (beer && mapping.kind == domain::PosItemKind::Package) {
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputInt("##pack", &mapping.pack_size, 0, 0);
            mapping.pack_size = std::max(mapping.pack_size, 1);
            if (finished()) save(item);
            ImGui::SetItemTooltip("Cans in each one sold: 1 for single cans, 4 for a 4-pack");
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void PosImportWindow::draw_import() {
    ImGui::SeparatorText("Import");
    std::set<domain::Date> days;
    for (const domain::PosRow& row : rows_.rows) days.insert(row.day);
    ImGui::Text("%zu %s, %s to %s", days.size(), days.size() == 1 ? "day" : "days", date_text(*days.begin()).c_str(),
                date_text(*days.rbegin()).c_str());

    ImGui::TextDisabled("These days' sales replace the taproom's expected revenue from the kegs sent to it.");
    ImGui::Checkbox("Take cans sold to go out of packaged goods", &take_cans_);
    ImGui::SetItemTooltip("Clear it if those cans already left packaged goods, on a sale to the taproom");
    if (days_imported_ > 0) {
        ImGui::TextColored(kWarningColor, "%d of these days %s imported before.", days_imported_,
                           days_imported_ == 1 ? "was" : "were");
        ImGui::SameLine();
        ImGui::Checkbox("Replace them", &replace_);
    }

    const auto unready = std::count_if(items_.begin(), items_.end(),
                                       [](const ItemTotal& item) { return !missing(item).empty(); });
    const bool enabled = unready == 0 && (days_imported_ == 0 || replace_);
    ImGui::BeginDisabled(!enabled);
    if (ImGui::Button("Import")) {
        try {
            const app::SalesService::PosImport made = sales_.import_pos(file_name_, rows_.rows, take_cans_, replace_);
            sales_.remember_pos_columns(signature_, columns_);
            result_ = "Imported " + std::to_string(made.days) + (made.days == 1 ? " day: " : " days: ") +
                      std::to_string(made.sales) + (made.sales == 1 ? " sale" : " sales");
            if (sales_.can_set_prices()) result_ += ", " + domain::format_money(made.revenue, sales_.currency());
            result_ += ".";
            if (made.replaced_days > 0) {
                result_ += " Replaced " + std::to_string(made.replaced_days) +
                           (made.replaced_days == 1 ? " day" : " days") + " imported before.";
            }
            if (made.netted_out > 0) {
                result_ += " Left out " + std::to_string(made.netted_out) +
                           (made.netted_out == 1 ? " item" : " items") + " refunded on the day it was sold.";
            }
            error_.clear();
            replace_ = false;
            days_imported_ = sales_.pos_days_imported(rows_.rows);
        } catch (const std::exception& e) {
            error_ = e.what();
            result_.clear();
        }
    }
    ImGui::EndDisabled();
    if (unready > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("%d %s still to say what %s", static_cast<int>(unready), unready == 1 ? "item" : "items",
                            unready == 1 ? "it is" : "they are");
    }
    if (!result_.empty()) ImGui::TextColored(kGoodColor, "%s", result_.c_str());
    if (!error_.empty()) ImGui::TextColored(kErrorColor, "%s", error_.c_str());
}

} // namespace ninkasi::ui
