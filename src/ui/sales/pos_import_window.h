#pragma once
// Sales > Import POS sales: the taproom's sales from a point-of-sale export,
// in four parts down one window. Choose the file; say which column holds
// what (guessed, and remembered for files like it), checking the preview;
// say what each item is (a pour of a beer, cans to go, something else, or
// not a sale), which is remembered too; then import. Each day becomes a sale
// in the taproom and one to go. Days imported before are replaced.

#include "app/batch_service.h"
#include "app/inventory_service.h"
#include "app/pos_import.h"
#include "app/recipe_service.h"
#include "app/sales_service.h"
#include "calc/units.h"
#include "domain/sales.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ninkasi::ui {

class PosImportWindow {
public:
    PosImportWindow(app::SalesService& sales, app::RecipeService& recipes, app::BatchService& batches,
                    const app::InventoryService& inventory, const calc::UnitPreferences& units, float ui_scale);

    void show();
    void draw();

private:
    // An item in the file, with what was sold of it and what it is.
    struct ItemTotal {
        std::string item;
        double quantity = 0.0;
        double amount = 0.0;
        domain::PosItem mapping;  // being edited; saved as each field is finished
        bool mapped = false;      // whether it's been said what it is
    };

    void choose_file();
    // Reads the rows again with the columns as they now are.
    void read_rows();
    void draw_file();
    void draw_columns();
    void draw_items();
    void draw_import();
    // A dropdown of the file's columns; `none` ("Each row is one") if it may be left out.
    bool column_combo(const char* label, int& column, const char* none);
    // What's still to say about an item before it can be imported; empty when nothing.
    static std::string missing(const ItemTotal& item);
    void save(ItemTotal& item);

    app::SalesService& sales_;
    app::RecipeService& recipes_;
    app::BatchService& batches_;
    const app::InventoryService& inventory_;
    const calc::UnitPreferences& units_;
    float ui_scale_;

    bool visible_ = false;
    bool focus_ = false;
    std::string file_name_;
    std::vector<std::vector<std::string>> table_;  // the file, its header first
    std::string signature_;                        // of its header
    app::PosColumns columns_;
    app::PosTable rows_;
    std::vector<ItemTotal> items_;  // in the order they first appear
    int days_imported_ = 0;         // of the file's days, imported before
    bool take_cans_ = true;
    bool replace_ = false;
    std::vector<std::string> beers_;
    std::vector<double> can_sizes_;
    std::string error_;
    std::string result_;
};

} // namespace ninkasi::ui
