#include "ui/inventory/inventory_views.h"

#include "ui/date_widgets.h"
#include "ui/display.h"
#include "ui/widgets.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>

namespace ninkasi::ui {

namespace {

using calc::Measure;
using domain::ItemCategory;
using domain::StockUnit;

constexpr const char* kPopupId = "###inventory_dialog";

constexpr ImGuiTableFlags kListFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                       ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;

std::string lowercase(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool is_blank(const std::string& text) {
    return text.find_first_not_of(" \t\r\n") == std::string::npos;
}

// "+50.0 lb" / "-12.5 lb": a change in stock.
std::string change_text(const domain::InventoryItem& item, double quantity, const calc::UnitPreferences& units) {
    return (quantity > 0.0 ? "+" : "") + stock_text(item, quantity, units);
}

} // namespace

std::optional<calc::Unit> stock_display_unit(const domain::InventoryItem& item, const calc::UnitPreferences& units) {
    switch (item.unit) {
    case StockUnit::Mass:
        if (item.category == ItemCategory::Hop) return units.unit_for(Measure::HopMass);
        if (item.category == ItemCategory::Yeast) return units.unit_for(Measure::AdditionMass);
        return units.unit_for(Measure::Mass);
    case StockUnit::Volume:
        if (item.category == ItemCategory::Yeast || item.category == ItemCategory::Chemical) {
            return small_volume_unit(units.unit_for(Measure::Volume));
        }
        return units.unit_for(Measure::Volume);
    case StockUnit::Count:
        return std::nullopt;
    }
    return std::nullopt;
}

std::string stock_text(const domain::InventoryItem& item, double quantity, const calc::UnitPreferences& units) {
    if (const std::optional<calc::Unit> unit = stock_display_unit(item, units)) return calc::format(quantity, *unit);
    const bool whole = std::abs(quantity - std::round(quantity)) < 1e-9;
    return number_text(quantity, whole ? 0 : 1, " ea");
}

InventoryViews::InventoryViews(app::InventoryService& inventory, const app::FloorService& floor,
                               app::IngredientLibrary& library, const calc::UnitPreferences& units, float ui_scale)
    : inventory_(inventory), floor_(floor), library_(library), units_(units), ui_scale_(ui_scale) {}

void InventoryViews::show_window() {
    visible_ = true;
    focus_ = true;
}

void InventoryViews::draw() {
    if (visible_) draw_window();
    draw_dialog();
}

// --- The Inventory window -------------------------------------------------------

void InventoryViews::draw_window() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(900.0f, 640.0f) * ui_scale_, ImGuiCond_FirstUseEver);
    const bool raise = focus_;
    if (focus_) {
        ImGui::SetNextWindowFocus();
        focus_ = false;
    }
    const bool expanded = ImGui::Begin("Inventory", &visible_);
    if (raise) raise_os_window();
    if (expanded && ImGui::BeginTabBar("##inventory_tabs")) {
        if (ImGui::BeginTabItem("Stock")) {
            draw_stock();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("In and out")) {
            draw_log();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void InventoryViews::draw_stock() {
    if (ImGui::Button("Receive...")) receive(selected_);
    ImGui::SameLine();
    if (ImGui::Button("New item...")) edit_item(std::nullopt);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f * ui_scale_);
    ImGui::InputTextWithHint("##filter", "Search", &filter_);
    ImGui::SameLine();
    ImGui::Checkbox("Low stock only", &low_only_);
    ImGui::Spacing();

    if (inventory_.stock().empty()) {
        wrapped_note("Nothing is stocked yet. Add the items you keep with New item, then log deliveries with "
                     "Receive. Knock-outs take the ingredients they used from here.");
        return;
    }

    // The item list on top, the selected item's lots and history below.
    const float list_height = selected_ ? ImGui::GetContentRegionAvail().y * 0.45f : 0.0f;
    if (ImGui::BeginChild("##items", ImVec2(0.0f, list_height))) draw_items();
    ImGui::EndChild();
    if (selected_ && inventory_.find(*selected_)) {
        ImGui::Separator();
        draw_item_details(*selected_);
    }
}

void InventoryViews::draw_log() {
    if (log_revision_ != inventory_.revision()) {
        log_ = inventory_.movements(std::nullopt, 300);
        log_revision_ = inventory_.revision();
    }
    draw_movements("##log", log_, true);
}

void InventoryViews::draw_items() {
    const std::string needle = lowercase(filter_);
    if (!ImGui::BeginTable("##item_list", 4, kListFlags | ImGuiTableFlags_ScrollY)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Category");
    ImGui::TableSetupColumn("On hand");
    ImGui::TableSetupColumn("Reorder below");
    ImGui::TableHeadersRow();

    for (const app::ItemStock& entry : inventory_.stock()) {
        const domain::InventoryItem& item = entry.item;
        if (low_only_ && !entry.low()) continue;
        if (!needle.empty() &&
            lowercase(item.name + " " + std::string(domain::name(item.category))).find(needle) == std::string::npos) {
            continue;
        }
        ImGui::PushID(static_cast<int>(item.id));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ImGui::Selectable(item.name.c_str(), selected_ == item.id, ImGuiSelectableFlags_SpanAllColumns)) {
            selected_ = item.id;
        }
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", domain::name(item.category).data());
        ImGui::TableNextColumn();
        const std::string on_hand = stock_text(item, entry.on_hand, units_);
        if (entry.on_hand < 0.0) {
            ImGui::TextColored(kErrorColor, "%s", on_hand.c_str());
            ImGui::SetItemTooltip("More was used than was recorded: log the delivery it came from, or count it.");
        } else if (entry.low()) {
            ImGui::TextColored(kWarningColor, "%s", on_hand.c_str());
            ImGui::SetItemTooltip("Low: below its reorder level");
        } else {
            ImGui::TextUnformatted(on_hand.c_str());
        }
        ImGui::TableNextColumn();
        if (item.reorder_level) ImGui::TextDisabled("%s", stock_text(item, *item.reorder_level, units_).c_str());
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void InventoryViews::draw_item_details(std::int64_t item_id) {
    const app::ItemStock& entry = *inventory_.find(item_id);
    const domain::InventoryItem item = entry.item;  // the buttons below can change the list
    if (loaded_item_ != item_id || loaded_revision_ != inventory_.revision()) {
        lots_ = inventory_.lots(item_id);
        history_ = inventory_.movements(item_id, 200);
        loaded_item_ = item_id;
        loaded_revision_ = inventory_.revision();
    }

    ImGui::TextUnformatted(item.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s on hand", stock_text(item, entry.on_hand, units_).c_str());
    // "##item": the window's toolbar has a Receive button too.
    if (ImGui::Button("Receive...##item")) receive(item_id);
    ImGui::SameLine();
    if (ImGui::Button("Count...")) count(item_id);
    ImGui::SameLine();
    if (ImGui::Button("Use or discard...")) take(item_id);
    ImGui::SameLine();
    if (ImGui::Button("Edit...")) edit_item(item_id);
    ImGui::SameLine();
    if (ImGui::Button("Remove")) {
        inventory_.remove_item(item_id);
        selected_.reset();
        return;
    }
    ImGui::SetItemTooltip("Items with history are kept for it but no longer listed.");

    if (!ImGui::BeginTabBar("##item_tabs")) return;
    if (ImGui::BeginTabItem("Lots")) {
        if (lots_.empty()) {
            wrapped_note("No deliveries logged yet.");
        } else if (ImGui::BeginTable("##lots", 6, kListFlags | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            for (const char* column : {"Received", "Supplier", "Lot", "Quantity", "Left", "Best before"}) {
                ImGui::TableSetupColumn(column);
            }
            ImGui::TableHeadersRow();
            for (auto it = lots_.rbegin(); it != lots_.rend(); ++it) {
                const data::LotRow& row = *it;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(date_text(domain::local_date(row.lot.received_at)).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.lot.supplier.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.lot.lot_code.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(stock_text(item, row.lot.quantity, units_).c_str());
                ImGui::TableNextColumn();
                if (row.remaining > 1e-9) {
                    ImGui::TextUnformatted(stock_text(item, row.remaining, units_).c_str());
                } else {
                    ImGui::TextDisabled("used up");
                }
                ImGui::TableNextColumn();
                if (row.lot.best_before) {
                    const bool expired = *row.lot.best_before < domain::today() && row.remaining > 1e-9;
                    ImGui::TextColored(expired ? kWarningColor : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s",
                                       date_text(*row.lot.best_before).c_str());
                }
            }
            ImGui::EndTable();
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("History")) {
        draw_movements("##history", history_, false);
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

void InventoryViews::draw_movements(const char* id, const std::vector<data::MovementRow>& rows, bool show_item) {
    if (rows.empty()) {
        wrapped_note("Nothing has come in or gone out yet.");
        return;
    }
    if (!ImGui::BeginTable(id, show_item ? 6 : 5, kListFlags | ImGuiTableFlags_ScrollY)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("When");
    if (show_item) ImGui::TableSetupColumn("Item");
    ImGui::TableSetupColumn("Change");
    ImGui::TableSetupColumn("What");
    ImGui::TableSetupColumn("Note", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Who");
    ImGui::TableHeadersRow();
    for (const data::MovementRow& row : rows) {
        // Enough of the item to format its quantities.
        domain::InventoryItem item;
        item.category = row.category;
        item.unit = row.unit;
        const domain::StockMovement& movement = row.movement;

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", time_text(movement.at).c_str());
        if (show_item) {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.item_name.c_str());
        }
        ImGui::TableNextColumn();
        ImGui::TextColored(movement.quantity > 0.0 ? kGoodColor : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s",
                           change_text(item, movement.quantity, units_).c_str());
        ImGui::TableNextColumn();
        std::string what(domain::name(movement.reason));
        if (!row.batch_code.empty()) what += " " + domain::batch_label(row.batch_code, "");
        if (!row.lot_code.empty()) what += ", lot " + row.lot_code;
        ImGui::TextUnformatted(what.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(movement.note.c_str());
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", movement.user.c_str());
    }
    ImGui::EndTable();
}

// --- Dialogs ----------------------------------------------------------------------

void InventoryViews::open(Kind kind) {
    kind_ = kind;
    open_requested_ = true;
    error_.clear();
    center_ = ImGui::GetWindowViewport()->GetCenter();
}

void InventoryViews::close() {
    ImGui::CloseCurrentPopup();
    kind_ = Kind::None;
    error_.clear();
}

void InventoryViews::edit_item(std::optional<std::int64_t> item_id) {
    if (item_id) {
        const app::ItemStock* entry = inventory_.find(*item_id);
        if (!entry) return;
        item_ = entry->item;
    } else {
        item_ = {};
    }
    has_reorder_ = item_.reorder_level.has_value();
    reorder_ = item_.reorder_level.value_or(0.0);
    open(Kind::EditItem);
}

void InventoryViews::receive(std::optional<std::int64_t> item_id) {
    item_id_ = item_id.value_or(0);
    at_ = domain::now();
    quantity_ = 0.0;
    supplier_.clear();
    lot_code_.clear();
    has_cost_ = false;
    cost_ = 0.0;
    has_best_before_ = false;
    best_before_ = domain::add_days(domain::today(), 365);
    note_.clear();
    open(Kind::Receive);
}

void InventoryViews::count(std::int64_t item_id) {
    const app::ItemStock* entry = inventory_.find(item_id);
    if (!entry) return;
    item_id_ = item_id;
    quantity_ = std::max(entry->on_hand, 0.0);
    at_ = domain::now();
    note_.clear();
    open(Kind::Count);
}

void InventoryViews::take(std::int64_t item_id) {
    item_id_ = item_id;
    quantity_ = 0.0;
    reason_ = domain::StockReason::Used;
    at_ = domain::now();
    note_.clear();
    open(Kind::Take);
}

void InventoryViews::draw_dialog() {
    if (open_requested_) {
        ImGui::OpenPopup(kPopupId);
        open_requested_ = false;
    }
    if (kind_ == Kind::None) return;

    const char* title = "";
    switch (kind_) {
    case Kind::EditItem: title = item_.id == 0 ? "New item" : "Edit item"; break;
    case Kind::Receive:  title = "Receive a delivery"; break;
    case Kind::Count:    title = "Count stock"; break;
    case Kind::Take:     title = "Use or discard stock"; break;
    case Kind::None:     break;
    }
    ImGui::SetNextWindowPos(center_, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    bool keep_open = true;
    if (!ImGui::BeginPopupModal((std::string(title) + kPopupId).c_str(), &keep_open,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        kind_ = Kind::None;
        return;
    }
    switch (kind_) {
    case Kind::EditItem: draw_item_editor(); break;
    case Kind::Receive:  draw_receive(); break;
    case Kind::Count:    draw_count(); break;
    case Kind::Take:     draw_take(); break;
    case Kind::None:     break;
    }
    ImGui::EndPopup();
}

void InventoryViews::draw_item_editor() {
    const float field = 280.0f * ui_scale_;
    if (ImGui::BeginTable("##item", 2, ImGuiTableFlags_SizingFixedFit)) {
        table_row("Name");
        ImGui::SetNextItemWidth(field);
        ImGui::InputText("##name", &item_.name);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(-1);

        table_row("Category");
        ImGui::SetNextItemWidth(field);
        enum_combo("##category", item_.category, domain::kItemCategories);

        // The library ingredient it stocks, so brews draw from it even when
        // the names differ.
        if (const std::optional<domain::IngredientKind> kind = domain::ingredient_kind_of(item_.category)) {
            std::vector<std::pair<std::int64_t, std::string>> choices;
            if (*kind == domain::IngredientKind::Fermentable) {
                for (const auto& entry : library_.fermentables()) choices.emplace_back(entry.id, entry.item.name);
            } else if (*kind == domain::IngredientKind::Hop) {
                for (const auto& entry : library_.hops()) choices.emplace_back(entry.id, entry.item.name);
            } else {
                for (const auto& entry : library_.yeasts()) choices.emplace_back(entry.id, entry.item.name);
            }
            const auto linked = std::find_if(choices.begin(), choices.end(),
                                             [this](const auto& choice) { return choice.first == item_.library_id; });
            table_row("Library ingredient");
            ImGui::SetNextItemWidth(field);
            if (ImGui::BeginCombo("##library", linked != choices.end() ? linked->second.c_str() : "Matched by name")) {
                if (ImGui::Selectable("Matched by name", linked == choices.end())) item_.library_id = 0;
                for (const auto& [id, name] : choices) {
                    ImGui::PushID(static_cast<int>(id));
                    if (ImGui::Selectable(name.c_str(), id == item_.library_id)) {
                        item_.library_id = id;
                        if (is_blank(item_.name)) item_.name = name;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("Brews draw on this item for that ingredient. Otherwise they draw on the item "
                                  "with the ingredient's name.");
        }

        table_row("Measured");
        ImGui::BeginDisabled(item_.id != 0);  // its history is in this unit
        ImGui::SetNextItemWidth(field);
        enum_combo("##unit", item_.unit, domain::kStockUnits);
        ImGui::EndDisabled();

        table_row("Reorder below");
        ImGui::Checkbox("##has_reorder", &has_reorder_);
        ImGui::SetItemTooltip("Flag it as low stock below this amount");
        if (has_reorder_) {
            ImGui::SameLine();
            quantity_field("##reorder", item_, reorder_);
        }

        table_row("Notes");
        ImGui::SetNextItemWidth(field);
        ImGui::InputText("##notes", &item_.notes);
        ImGui::EndTable();
    }

    if (!buttons(item_.id == 0 ? "Add item" : "Save", !is_blank(item_.name))) return;
    item_.reorder_level = has_reorder_ ? std::optional<double>(reorder_) : std::nullopt;
    try {
        if (item_.id == 0) {
            selected_ = inventory_.add_item(item_);
        } else {
            inventory_.update_item(item_);
        }
        close();
    } catch (const std::exception& e) {
        error_ = e.what();
    }
}

void InventoryViews::draw_receive() {
    const app::ItemStock* entry = inventory_.find(item_id_);
    if (ImGui::BeginTable("##receive", 2, ImGuiTableFlags_SizingFixedFit)) {
        table_row("Item");
        item_combo("##item", item_id_);
        entry = inventory_.find(item_id_);
        if (entry) {
            table_row("Quantity");
            quantity_field("##quantity", entry->item, quantity_);
        }
        table_row("Received");
        time_picker("##received", at_);
        table_row("Supplier");
        ImGui::SetNextItemWidth(220.0f * ui_scale_);
        ImGui::InputText("##supplier", &supplier_);
        table_row("Lot");
        ImGui::SetNextItemWidth(220.0f * ui_scale_);
        ImGui::InputTextWithHint("##lot", "The supplier's lot number", &lot_code_);
        table_row("Cost");
        ImGui::Checkbox("##has_cost", &has_cost_);
        if (has_cost_) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f * ui_scale_);
            edit_number("##cost", cost_, 2);
        }
        table_row("Best before");
        ImGui::Checkbox("##has_best_before", &has_best_before_);
        if (has_best_before_) {
            ImGui::SameLine();
            date_picker("##best_before", best_before_);
        }
        table_row("Note");
        ImGui::SetNextItemWidth(220.0f * ui_scale_);
        ImGui::InputText("##note", &note_);
        ImGui::EndTable();
    }
    if (inventory_.stock().empty()) wrapped_note("Add the item first with New item.");

    if (!buttons("Receive", entry && quantity_ > 0.0)) return;
    try {
        inventory_.receive({.item_id = item_id_,
                            .at = at_,
                            .quantity = quantity_,
                            .supplier = supplier_,
                            .lot_code = lot_code_,
                            .cost = has_cost_ ? std::optional<double>(cost_) : std::nullopt,
                            .best_before = has_best_before_ ? std::optional<domain::Date>(best_before_) : std::nullopt,
                            .note = note_});
        selected_ = item_id_;
        close();
    } catch (const std::exception& e) {
        error_ = e.what();
    }
}

void InventoryViews::draw_count() {
    const app::ItemStock* entry = inventory_.find(item_id_);
    if (!entry) return close();
    ImGui::TextUnformatted(entry->item.name.c_str());
    if (ImGui::BeginTable("##count", 2, ImGuiTableFlags_SizingFixedFit)) {
        table_row("Recorded");
        ImGui::TextUnformatted(stock_text(entry->item, entry->on_hand, units_).c_str());
        table_row("Counted");
        quantity_field("##counted", entry->item, quantity_);
        table_row("When");
        time_picker("##at", at_);
        table_row("Note");
        ImGui::SetNextItemWidth(220.0f * ui_scale_);
        ImGui::InputText("##note", &note_);
        ImGui::EndTable();
    }
    if (!buttons("Save count", quantity_ >= 0.0)) return;
    try {
        inventory_.count(item_id_, quantity_, at_, note_);
        close();
    } catch (const std::exception& e) {
        error_ = e.what();
    }
}

void InventoryViews::draw_take() {
    const app::ItemStock* entry = inventory_.find(item_id_);
    if (!entry) return close();
    ImGui::Text("%s: %s on hand", entry->item.name.c_str(), stock_text(entry->item, entry->on_hand, units_).c_str());
    if (ImGui::BeginTable("##take", 2, ImGuiTableFlags_SizingFixedFit)) {
        table_row("");
        if (ImGui::RadioButton("Used", reason_ == domain::StockReason::Used)) reason_ = domain::StockReason::Used;
        ImGui::SameLine();
        if (ImGui::RadioButton("Discarded", reason_ == domain::StockReason::Discarded)) {
            reason_ = domain::StockReason::Discarded;
        }
        table_row("Quantity");
        quantity_field("##quantity", entry->item, quantity_);
        table_row("When");
        time_picker("##at", at_);
        table_row("Note");
        ImGui::SetNextItemWidth(220.0f * ui_scale_);
        ImGui::InputTextWithHint("##note", "What for, or why", &note_);
        ImGui::EndTable();
    }
    wrapped_note("Brews take what they use at knock-out; this is for everything else.");
    if (!buttons("Save", quantity_ > 0.0)) return;
    try {
        inventory_.take(item_id_, quantity_, reason_, at_, note_);
        close();
    } catch (const std::exception& e) {
        error_ = e.what();
    }
}

// --- Helpers ----------------------------------------------------------------------

bool InventoryViews::buttons(const char* confirm, bool enabled) {
    if (!error_.empty()) ImGui::TextColored(kErrorColor, "%s", error_.c_str());
    ImGui::Spacing();
    ImGui::BeginDisabled(!enabled);
    bool pressed = ImGui::Button(confirm);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) close();
    // Enter saves; a field being typed in has already taken its value.
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    if (enabled && enter && !ImGui::IsAnyItemActive()) pressed = true;
    if (enabled) {
        ImGui::SameLine();
        ImGui::TextDisabled("Enter saves");
    }
    return pressed;
}

bool InventoryViews::item_combo(const char* label, std::int64_t& item_id) {
    const app::ItemStock* chosen = inventory_.find(item_id);
    bool changed = false;
    ImGui::SetNextItemWidth(280.0f * ui_scale_);
    if (ImGui::BeginCombo(label, chosen ? chosen->item.name.c_str() : "Choose an item")) {
        for (const app::ItemStock& entry : inventory_.stock()) {
            ImGui::PushID(static_cast<int>(entry.item.id));
            if (ImGui::Selectable(entry.item.name.c_str(), entry.item.id == item_id)) {
                item_id = entry.item.id;
                changed = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s on hand", stock_text(entry.item, entry.on_hand, units_).c_str());
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

void InventoryViews::quantity_field(const char* label, const domain::InventoryItem& item, double& quantity) {
    ImGui::SetNextItemWidth(120.0f * ui_scale_);
    if (const std::optional<calc::Unit> unit = stock_display_unit(item, units_)) {
        edit_quantity(label, quantity, *unit);
    } else {
        edit_number(label, quantity, 0, "ea");
    }
}

} // namespace ninkasi::ui
