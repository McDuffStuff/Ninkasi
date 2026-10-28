#pragma once
// Inventory on screen: every item with what's on hand, its lots and history,
// the invoices deliveries arrived on, and the log of everything in and out,
// shown in the Inventory window and in the window of each Inventory element
// on the floor; and the dialogs for adding items, receiving deliveries,
// counting stock and using or discarding it.
//
// Like the cellar dialogs, these log what already happened: fill them in and
// press Enter. The exception is an invoice, which is built up line by line
// and saved with its Submit button.

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
// A quantity of an item with its unit: "450.0 lb", "12 ea", "2400 cans".
std::string stock_text(const domain::InventoryItem& item, double quantity, const calc::UnitPreferences& units);
// The unit for what a can holds: US fluid ounces for breweries using US
// volumes, otherwise milliliters.
calc::Unit fill_volume_unit(const calc::UnitPreferences& units);

class InventoryViews {
public:
    InventoryViews(app::InventoryService& inventory, const app::FloorService& floor, app::IngredientLibrary& library,
                   const calc::UnitPreferences& units, float ui_scale);

    void show_window();
    bool* window_visible() { return &visible_; }

    // Call once per frame, outside any window: the Inventory window and the dialogs.
    void draw();

    // The stock (the toolbar, every item, and the selected item's lots and
    // history), the invoices received and the log of everything in and out.
    // The Inventory window shows these, and so do Inventory elements' windows.
    void draw_stock();
    void draw_invoices();
    void draw_log();

private:
    enum class Kind { None, EditItem, Invoice, Count, Take };
    // Which of a dialog's buttons was used this frame.
    enum class Choice { None, Confirm, Cancel };

    // A line of the invoice being entered.
    struct DraftLine {
        std::int64_t item_id = 0;  // 0: blank
        double quantity = 0.0;
        double cost = 0.0;         // 0: not entered
        std::string lot_code;
        bool has_best_before = false;
        domain::Date best_before{};
    };

    void draw_window();
    // Items in a table, filtered by the search and low-stock filter;
    // clicking one selects it.
    void draw_items();
    void draw_item_details(std::int64_t item_id);
    void draw_movements(const char* id, const std::vector<data::MovementRow>& rows, bool show_item);

    void open(Kind kind);
    void close();
    void edit_item(std::optional<std::int64_t> item_id);
    // Starts an invoice, with a line for `item_id` if given.
    void receive(std::optional<std::int64_t> item_id);
    void count(std::int64_t item_id);
    void take(std::int64_t item_id);
    void draw_dialog();
    // The fields of the item being added or edited (item_).
    void draw_item_fields();
    // Saves item_; its id, or nullopt with `error` set if it was refused.
    std::optional<std::int64_t> save_item(std::string& error);
    void draw_item_editor();
    void draw_invoice();
    // An item added from an invoice line, in a dialog over the invoice.
    void draw_invoice_new_item();
    void draw_count();
    void draw_take();
    // The error, if any, then the confirm and Cancel buttons. With `keys`,
    // Enter confirms and Escape cancels.
    Choice dialog_buttons(const char* confirm, bool enabled, const std::string& error, bool keys);
    // The open dialog's buttons: Cancel closes it. True when confirmed.
    bool buttons(const char* confirm, bool enabled);
    // An item picker ending with "New item...", which sets `new_item`.
    // Returns true when the choice changes.
    bool item_combo(const char* label, std::int64_t& item_id, bool& new_item);
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

    // Invoices, and the lines of the selected one, re-read when inventory changes.
    std::uint64_t invoices_revision_ = 0;
    std::vector<data::InvoiceRow> invoices_;
    std::optional<std::int64_t> selected_invoice_;
    std::int64_t loaded_invoice_ = 0;
    std::uint64_t lines_revision_ = 0;
    std::vector<data::InvoiceLineRow> invoice_lines_;

    // The open dialog and its form.
    Kind kind_ = Kind::None;
    bool open_requested_ = false;
    ImVec2 center_{};
    std::string error_;
    domain::InventoryItem item_;   // the item being added or edited
    std::int64_t item_id_ = 0;     // the item being counted or taken
    domain::Timestamp at_{};
    double quantity_ = 0.0;
    bool has_reorder_ = false;
    double reorder_ = 0.0;
    domain::StockReason reason_ = domain::StockReason::Used;
    std::string note_;

    // The invoice being entered. Its last line is always blank, ready for the next item.
    std::string supplier_;
    std::string invoice_number_;
    std::vector<DraftLine> lines_;
    bool open_new_item_ = false;
    std::size_t new_item_line_ = 0;  // the line an item is being added for
    std::string new_item_error_;
};

} // namespace ninkasi::ui
