#pragma once
// The figures behind the Insights window, worked out from the ledgers for a
// period, split into buckets (weeks, months or quarters) and compared with
// another period.
//
// Volumes, rates, stock, ingredient use and production are everyone's.
// Money (revenue, margins, costs, prices paid, the P/L) is the brewery's,
// so it's worked out only for admins: for anyone else those figures are
// left empty, and batch_costs() and profit_loss() throw NotAllowed.
//
// Beer counts as sold when it leaves packaged goods for a sale, the
// taproom, export or ships' supplies (see domain::counts_as_sold), whether
// or not a sale was recorded for it; revenue comes from the sales recorded.

#include "app/costing_service.h"
#include "app/expense_service.h"
#include "app/session.h"
#include "app/ttb_service.h"
#include "data/analytics_repository.h"
#include "data/database.h"
#include "data/settings_repository.h"
#include "domain/analytics.h"
#include "domain/sales.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ninkasi::app {

struct InsightsQuery {
    domain::Period period;
    domain::Grouping grouping = domain::Grouping::Month;
    domain::Comparison comparison = domain::Comparison::PreviousPeriod;
};

// A figure over the period, over the one it's compared with, and per bucket.
struct Figure {
    double now = 0.0;
    std::optional<double> before;
    std::vector<double> series;  // by bucket

    std::optional<double> change() const;
};

// --- Overview ---------------------------------------------------------------------

struct Overview {
    std::vector<domain::Bucket> buckets;
    std::optional<Figure> revenue;       // admins
    std::optional<Figure> gross_margin;  // admins: a fraction of revenue
    Figure sold_l;
    Figure brewed_l;
    std::optional<double> inventory_value;  // admins: today
    int kegs_out = 0;                       // at customers, today
    int beers_to_brew = 0;                  // brew soon or now
};

// --- Sales ------------------------------------------------------------------------

// Revenue (admins) and volume by channel, per bucket. Volume sold without
// a recorded sale is under the channel its removal implies: the taproom's
// own, or Other for beer simply taken out as sold.
struct ChannelSeries {
    domain::Channel channel = domain::Channel::Other;
    std::vector<double> revenue;
    std::vector<double> volume_l;
};

// One beer's sales.
struct BeerSales {
    std::string beer;
    double volume_l = 0.0;  // sold in the period
    std::optional<double> volume_before;
    std::map<domain::PackageType, int> units;  // packages of each kind sold in the period
    double revenue = 0.0;              // admins
    double per_week_l = 0.0;           // over the period
    double lifetime_l = 0.0;
    double lifetime_revenue = 0.0;     // admins
};

struct SalesInsights {
    std::vector<domain::Bucket> buckets;
    std::vector<ChannelSeries> channels;  // those with any sales, in kChannels order
    std::vector<BeerSales> beers;         // most sold first
    int unpriced_lines = 0;               // admins: sale lines in the period still without a price
};

// A beer's sales, looked at closely.
struct BeerDetail {
    std::string beer;
    std::vector<domain::Bucket> months;  // every month it sold in, to now
    std::vector<double> monthly_l;
    struct Customer {
        std::string name;
        double volume_l = 0.0;
        double revenue = 0.0;  // admins
    };
    std::vector<Customer> customers;  // in the period, most first
    struct Batch {
        std::string code;
        double volume_l = 0.0;
    };
    std::vector<Batch> batches;  // its beer sold in the period, by batch
};

// --- Stock and planning -----------------------------------------------------------

struct BeerRunway {
    std::string beer;
    double packaged_l = 0.0;  // on hand
    double in_tanks_l = 0.0;  // in batches not yet finished
    int tank_days = 0;        // brewing to packaging, lately
    domain::Runway runway;    // of both
};

// --- Ingredients ------------------------------------------------------------------

struct ItemUse {
    domain::InventoryItem item;
    double used = 0.0;  // in the period, in its stock unit
    double per_week = 0.0;  // lately
    double on_hand = 0.0;
    std::optional<int> days_to_reorder;  // at that rate, until it's below its reorder level
    std::optional<int> days_left;
    std::vector<double> series;  // used, by bucket
};

struct PricePoint {
    domain::Date day{};
    double unit_cost = 0.0;
    std::string supplier;
};

struct IngredientInsights {
    std::vector<domain::Bucket> buckets;
    std::vector<ItemUse> items;                                // most used first
    std::map<std::int64_t, std::vector<PricePoint>> prices;    // admins: by item, each lot received
    std::vector<std::pair<std::string, double>> suppliers;     // admins: spend in the period, most first
    std::optional<double> stock_value;                         // admins: today
};

// --- Production -------------------------------------------------------------------

struct BatchLosses {
    std::int64_t batch_id = 0;
    std::string code;
    std::string beer;
    double brewed_l = 0.0;
    double packaged_l = 0.0;
    double transfer_loss_l = 0.0;  // left behind by transfers and packaging runs
    double dumped_l = 0.0;
    double adjusted_l = 0.0;       // measured short (positive) or over (negative)
    std::optional<int> days_in_tanks;

    double lost_share() const { return brewed_l > 0.0 ? (brewed_l - packaged_l) / brewed_l : 0.0; }
};

struct ProductionInsights {
    std::vector<domain::Bucket> buckets;
    Figure brewed_l;
    Figure packaged_l;
    std::vector<BatchLosses> batches;  // finished in the period, newest first
    struct TankUse {
        std::string vessel;
        std::vector<double> share;  // by bucket, 0 to 1
    };
    std::vector<TankUse> tanks;
    struct TankTime {
        std::string beer;
        int median_days = 0;
        int batches = 0;
    };
    std::vector<TankTime> tank_times;
};

// --- Batch costs (admins) -----------------------------------------------------------

struct BatchCostRow {
    domain::BatchFacts batch;
    domain::BatchCost cost;
    double revenue = 0.0;  // of its beer sold on sales recorded
    double cost_sold = 0.0;  // of that beer
    std::optional<double> margin() const;
};

// --- Profit and loss (admins) -------------------------------------------------------

struct ProfitLossColumn {
    std::map<domain::Channel, double> revenue;
    double total_revenue = 0.0;
    double cost_of_goods = 0.0;
    double gross_profit = 0.0;
    double excise = 0.0;
    double beer_lost = 0.0;  // given away, lost, destroyed or dumped, at cost
    std::map<std::int64_t, double> expenses;  // by category
    double total_expenses = 0.0;
    double net = 0.0;

    std::optional<double> gross_margin() const;
};

struct ProfitLoss {
    std::vector<domain::Bucket> buckets;
    ProfitLossColumn now;
    std::optional<ProfitLossColumn> before;
    std::vector<double> revenue_series;  // by bucket
    std::vector<double> cost_series;     // cost of goods, excise, beer lost and expenses
    struct BeerMargin {
        std::string beer;
        double revenue = 0.0;
        double cost = 0.0;
        std::optional<double> margin() const;
    };
    std::vector<BeerMargin> beers;  // most revenue first
    // How sure it is.
    int unpriced_lines = 0;
    int uncosted_removals = 0;
    int estimated_batches = 0;  // batches sold whose cost is partly estimated or unknown
};

class InsightsService {
public:
    // `stamp` says when the ledgers may have changed: the services' revisions, combined.
    InsightsService(data::Database& db, Session session, data::SettingsRepository& settings, TtbService& ttb,
                    ExpenseService& expenses, CostingService& costing, std::function<std::uint64_t()> stamp);

    // Whether money is worked out (for admins).
    bool money() const { return session_.is_admin(); }
    // Reads the ledgers again if they've changed.
    void refresh();
    // Grows each time they're read again.
    std::uint64_t revision() const { return revision_; }

    // Days from brewing to packaging when a beer hasn't been brewed enough to
    // tell, and the weeks sales rates are taken over.
    int default_tank_days() const;
    int rate_weeks() const;
    void set_planning(int default_tank_days, int rate_weeks);

    Overview overview(const InsightsQuery& query) const;
    SalesInsights sales(const InsightsQuery& query) const;
    BeerDetail beer_detail(const std::string& beer, const InsightsQuery& query) const;
    // The sales recorded from `from` to `to`, newest first: the rows behind a bar.
    std::vector<domain::Sale> sales_between(domain::Date from, domain::Date to) const;
    std::vector<BeerRunway> stock(domain::Date today) const;
    IngredientInsights ingredients(const InsightsQuery& query) const;
    ProductionInsights production(const InsightsQuery& query) const;
    std::vector<BatchCostRow> batch_costs() const;
    ProfitLoss profit_loss(const InsightsQuery& query) const;

private:
    // Beer sold (see the class comment) between two days, by beer.
    std::map<std::string, double> sold_by_beer(domain::Date from, domain::Date to) const;
    double sold_l(domain::Date from, domain::Date to) const;
    double brewed_l(domain::Date from, domain::Date to) const;
    double revenue(domain::Date from, domain::Date to) const;
    ProfitLossColumn profit_loss_column(domain::Date from, domain::Date to) const;
    // The beer a batch is, by its id.
    const domain::BatchFacts* batch(std::int64_t id) const;
    // Days each batch of a beer took from brewing to packaging, oldest first.
    std::map<std::string, std::vector<int>> tank_days_by_beer() const;
    Figure figure(const InsightsQuery& query, const std::vector<domain::Bucket>& buckets,
                  const std::function<double(domain::Date, domain::Date)>& over) const;

    data::Database& db_;
    Session session_;
    data::SettingsRepository& settings_;
    TtbService& ttb_;
    ExpenseService& expenses_;
    CostingService& costing_;
    std::function<std::uint64_t()> stamp_;
    std::optional<std::uint64_t> loaded_stamp_;
    data::AnalyticsData data_;
    std::map<std::int64_t, const domain::Sale*> sale_of_line_;  // by sale line id
    std::uint64_t revision_ = 0;
};

} // namespace ninkasi::app
