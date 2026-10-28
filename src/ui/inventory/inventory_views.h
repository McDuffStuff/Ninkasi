#pragma once
// Inventory on screen: every item with what's on hand, its lots and history,
// and the log of everything in and out, shown in the Inventory window and in
// the window of each Inventory element on the floor; and the dialogs for
// adding items, receiving deliveries, counting stock and using or discarding
// it. Like the cellar dialogs, these log what already happened: fill them in
// and press Enter.

#include "app/floor_service.h"
#include "app/ingredient_library.h"
#include "app/inventory_service.h"
#include "calc/units.h"
#include "domain/inventory.h"

#include <imgui.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ninkasi::ui {

// The unit an item's quantities are shown in, or nullopt for counted items.
std::optional<calc::Unit> stock_display_unit(const domain::InventoryItem& item, const calc::UnitPreferences& units);
// A quantity of an item with its unit: "450.0 lb", "12 ea".
std::string stock_text(const domain::InventoryItem& item, double quantity, const calc::UnitPreferences& units);

class InventoryViews {
public:
    InventoryViews(app::InventoryService& inventory, const app::FloorService& floor, app::IngredientLibrary& library,
                   const calc::UnitPreferences& units, float ui_scale);

    void show_window();
    bool* window_visible() { return &visible_; }

    // Call once per frame, outside any window: the Inventory window and the dialogs.
    void draw();

    // The stock (the toolbar, every item, and the selected item's lots and
    // history) and the log of everything in and out. The Inventory window
    // shows these, and so do Inventory elements' windows.
    void draw_stock();
    void draw_log();

private:
    enum class Kind { None, EditItem, Receive, Count, Take };

    void draw_window();
    // Items in a table, filtered by the search and low-stock filter;
    // clicking one selects it.
    void draw_items();
    void draw_item_details(std::int64_t item_id);
    void draw_movements(const char* id, const std::vector<data::MovementRow>& rows, bool show_item);

    void open(Kind kind);
    void close();
    void edit_item(std::optional<std::int64_t> item_id);
    void receive(std::optional<std::int64_t> item_id);
    void count(std::int64_t item_id);
    void take(std::int64_t item_id);
    void draw_dialog();
    void draw_item_editor();
    void draw_receive();
    void draw_count();
    void draw_take();
    // The error, if any, then the confirm and Cancel buttons; true when the
    // confirm button or Enter is pressed.
    bool buttons(const char* confirm, bool enabled);
    // An item picker; returns true when the choice changes.
    bool item_combo(const char* label, std::int64_t& item_id);
    // A quantity field in the item's unit.
    void quantity_field(const char* label, const domain::InventoryItem& item, double& quantity);

    app::InventoryService& inventory_;
    const app::FloorService& floor_;
    app::IngredientLibrary& library_;
    const calc::UnitPreferences& units_;
    float ui_scale_;

    bool visible_ = false;
    bool focus_ = false;
    std::string filter_;
    bool low_only_ = false;
    std::optional<std::int64_t> selected_;

    // Lots and history of the selected item, and the log, re-read when
    // inventory changes.
    std::uint64_t loaded_revision_ = 0;
    std::int64_t loaded_item_ = 0;
    std::vector<data::LotRow> lots_;
    std::vector<data::MovementRow> history_;
    std::uint64_t log_revision_ = 0;
    std::vector<data::MovementRow> log_;

    // The open dialog and its form.
    Kind kind_ = Kind::None;
    bool open_requested_ = false;
    ImVec2 center_{};
    std::string error_;
    domain::InventoryItem item_;   // the item being added or edited
    std::int64_t item_id_ = 0;     // the item being received, counted or taken
    domain::Timestamp at_{};
    double quantity_ = 0.0;
    std::string supplier_;
    std::string lot_code_;
    bool has_cost_ = false;
    double cost_ = 0.0;
    bool has_best_before_ = false;
    domain::Date best_before_{};
    bool has_reorder_ = false;
    double reorder_ = 0.0;
    domain::StockReason reason_ = domain::StockReason::Used;
    std::string note_;
};

} // namespace ninkasi::ui
