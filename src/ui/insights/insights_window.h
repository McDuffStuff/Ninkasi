#pragma once
// View > Insights: the brewery's figures over time, in tabs. One row of
// controls above them all chooses the period, how it's split (weeks,
// months, quarters) and what it's compared with.
//
//   Overview         figures at a glance, each against the comparison
//   Sales            by channel over time, and by beer; click one for more
//   Stock & planning how long each beer lasts, and when to brew it
//   Ingredients      what's used, how fast, what it cost and who was paid
//   Production       brewed and packaged, losses, tank use
//   Batch costs      what each batch cost and earned (admins)
//   Profit & loss    the statement for the period (admins)
//
// Every chart can be shown as a table and saved as CSV. Brewers see
// volumes; money is for admins.

#include "app/expense_service.h"
#include "app/insights_service.h"
#include "app/sales_service.h"
#include "calc/units.h"
#include "domain/analytics.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ninkasi::ui {

class InsightsWindow {
public:
    InsightsWindow(app::InsightsService& insights, const app::SalesService& sales, const app::ExpenseService& expenses,
                   const calc::UnitPreferences& units, float ui_scale);

    void show();
    void draw();

private:
    void draw_controls();
    void draw_overview();
    void draw_sales();
    void draw_sales_bucket(std::size_t bucket);
    void draw_beer_detail();
    void draw_stock();
    void draw_ingredients();
    void draw_production();
    void draw_batch_costs();
    void draw_profit_loss();
    // Forgets the figures worked out, if the query or the ledgers changed.
    void invalidate_if_stale();

    // How values read.
    std::string money(double amount) const;
    std::string volume(double liters) const;  // in the brewery's volume unit
    double in_volume_unit(double liters) const;
    std::string volume_unit() const;

    app::InsightsService& insights_;
    const app::SalesService& sales_;  // for the currency
    const app::ExpenseService& expenses_;  // for category names
    const calc::UnitPreferences& units_;
    float ui_scale_;

    bool visible_ = false;
    bool focus_ = false;

    // The controls.
    int preset_ = 2;  // into domain::kPeriodPresets, or -1 for custom days
    app::InsightsQuery query_;
    bool grouping_chosen_ = false;  // otherwise it follows the period's length

    // Figures worked out for the query, kept until it or the ledgers change.
    std::uint64_t computed_revision_ = 0;
    std::optional<app::InsightsQuery> computed_query_;
    std::optional<app::Overview> overview_;
    std::optional<app::SalesInsights> sales_insights_;
    std::optional<app::BeerDetail> beer_detail_;
    std::optional<std::vector<app::BeerRunway>> stock_;
    std::optional<app::IngredientInsights> ingredients_;
    std::optional<app::ProductionInsights> production_;
    std::optional<std::vector<app::BatchCostRow>> batch_costs_;
    std::optional<app::ProfitLoss> profit_loss_;

    // Choices within the tabs.
    bool sales_by_revenue_ = true;
    std::optional<std::size_t> sales_bucket_;  // a bar clicked
    std::vector<domain::Sale> bucket_sales_;
    std::string selected_beer_;
    std::int64_t selected_item_ = 0;
    std::string error_;
    // Each chart's Table/Chart switch, by chart.
    bool table_sales_ = false;
    bool table_beer_ = false;
    bool table_customers_ = false;
    bool table_use_ = false;
    bool table_prices_ = false;
    bool table_suppliers_ = false;
    bool table_production_ = false;
    bool table_tanks_ = false;
    bool table_pl_ = false;
    bool table_overview_ = false;
    int tank_days_ = 21;   // planning settings being edited
    int rate_weeks_ = 8;
};

} // namespace ninkasi::ui
