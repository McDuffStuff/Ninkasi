// ninkasi_demo: makes a demo brewery, months of a small brewery's life up
// to today (a year unless told), for trying Ninkasi and checking its
// Insights by eye.
//
//   ninkasi_demo <new database file> [months]
//
// Everything goes through Ninkasi's own services, as if a brewer had
// entered it day by day, so the data is consistent the way real data is:
// deliveries of malt, hops and cans (prices drifting up, the odd one logged
// late); a brew every nine days or so of four beers in rotation, packaged
// into kegs and cans when ready; a distributor's weekly order, retail
// accounts, taproom kegs used up by the pour and cans to go, more in summer;
// kegs coming back with their deposits refunded; rent, wages and bills. The
// random choices come from a fixed seed, so every run makes the same
// brewery (for the same "today"). Beside the database it writes
// pos-export.csv, the last two weeks of a taproom point of sale, to try the
// POS import with. Not shipped with Ninkasi.

#include "app/batch_service.h"
#include "app/expense_service.h"
#include "app/floor_service.h"
#include "app/inventory_service.h"
#include "app/packaged_goods_service.h"
#include "app/sales_service.h"
#include "app/session.h"
#include "calc/units.h"
#include "data/database.h"
#include "data/migrations.h"
#include "data/settings_repository.h"
#include "domain/expenses.h"
#include "domain/timestamp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

using namespace ninkasi;
using domain::Date;
using domain::PackageType;

namespace {

constexpr double kBarrel = 31.0 * calc::kLitersPerUsGallon;  // liters
constexpr double kPint = 0.473;                                // liters: a 16 fl oz can or pour

domain::Timestamp at(Date day, int hour, int minute = 0) {
    return domain::from_local({day.year, day.month, day.day, hour, minute});
}

// A beer the brewery makes: what goes in a 10-barrel brew, and how long it
// takes from brewing to packaging.
struct Beer {
    std::string name;
    std::string style;
    int tank_days = 14;
    double popularity = 1.0;  // relative demand
    std::vector<std::pair<std::string, double>> ingredients;  // item name, kilograms (or packs)
};

const std::vector<Beer> kBeers{
    {"Red Ale", "Irish Red Ale", 16, 1.0,
     {{"Pale malt", 190.0}, {"Crystal 60", 22.0}, {"Roasted barley", 4.0}, {"Centennial", 2.0}, {"Dry ale yeast", 2.0}}},
    {"Hazy IPA", "Hazy IPA", 14, 1.5,
     {{"Pale malt", 200.0}, {"Flaked oats", 30.0}, {"Citra", 9.0}, {"Centennial", 3.0}, {"Dry ale yeast", 2.0}}},
    {"Pils", "German Pils", 28, 0.8, {{"Pilsner malt", 210.0}, {"Saaz", 4.0}, {"Lager yeast", 3.0}}},
    {"Oatmeal Stout", "Oatmeal Stout", 18, 0.6,
     {{"Pale malt", 180.0}, {"Flaked oats", 20.0}, {"Roasted barley", 18.0}, {"Crystal 60", 10.0}, {"Centennial", 1.5},
      {"Dry ale yeast", 2.0}}},
};

// What the brewery stocks: name, category, unit, price a unit to begin
// with, a delivery's size, the level it's reordered at, and who sells it.
struct Stocked {
    std::string name;
    domain::ItemCategory category;
    domain::StockUnit unit;
    double price;
    double delivery;
    double reorder;
    std::string supplier;
};

const std::vector<Stocked> kStock{
    {"Pale malt", domain::ItemCategory::Grain, domain::StockUnit::Mass, 1.30, 1000.0, 400.0, "Country Malt Group"},
    {"Pilsner malt", domain::ItemCategory::Grain, domain::StockUnit::Mass, 1.45, 500.0, 230.0, "Country Malt Group"},
    {"Crystal 60", domain::ItemCategory::Grain, domain::StockUnit::Mass, 1.90, 100.0, 40.0, "Country Malt Group"},
    {"Roasted barley", domain::ItemCategory::Grain, domain::StockUnit::Mass, 2.10, 50.0, 25.0, "Country Malt Group"},
    {"Flaked oats", domain::ItemCategory::Adjunct, domain::StockUnit::Mass, 1.60, 100.0, 40.0, "Country Malt Group"},
    {"Citra", domain::ItemCategory::Hop, domain::StockUnit::Mass, 31.0, 20.0, 10.0, "Yakima Chief Hops"},
    {"Centennial", domain::ItemCategory::Hop, domain::StockUnit::Mass, 18.0, 10.0, 4.0, "Yakima Chief Hops"},
    {"Saaz", domain::ItemCategory::Hop, domain::StockUnit::Mass, 21.0, 10.0, 5.0, "Hopsteiner"},
    {"Dry ale yeast", domain::ItemCategory::Yeast, domain::StockUnit::Count, 95.0, 10.0, 4.0, "Lallemand"},
    {"Lager yeast", domain::ItemCategory::Yeast, domain::StockUnit::Count, 105.0, 6.0, 3.0, "Lallemand"},
    {"16 oz cans", domain::ItemCategory::Can, domain::StockUnit::Count, 0.16, 6000.0, 2500.0, "Ball Cans"},
};

// A fermenter, and the batch in it.
struct Tank {
    std::int64_t id = 0;
    bool full = false;
    std::size_t beer = 0;
    Date ready{};
};

class DemoBrewery {
public:
    explicit DemoBrewery(const std::filesystem::path& path)
        : db_(path.string()), session_(start(db_)), settings_(db_), floor_(db_, session_), inventory_(db_, session_),
          goods_(db_, session_), batches_(db_, session_, floor_, inventory_, goods_),
          sales_(db_, session_, settings_, goods_), expenses_(db_, session_), random_(20261029) {}

    // The year from `first` to `last`, brewing from the day after the first.
    void make(Date first, Date last) {
        first_brew_ = domain::add_days(first, 1);
        set_up(first, last);
        for (Date day = first; day <= last; day = domain::add_days(day, 1)) live(day, day == last);
    }

    // The last two weeks of a taproom point of sale, as Square writes it.
    void write_pos_export(const std::filesystem::path& path, Date last) {
        std::ofstream file(path, std::ios::binary);
        file << "Date,Time,Category,Item,Qty,Gross Sales,Discounts,Net Sales,Tax\r\n";
        for (Date day = domain::add_days(last, -13); day <= last; day = domain::add_days(day, 1)) {
            for (const Beer& beer : kBeers) {
                const int pints = 8 + static_cast<int>(uniform(0.0, 20.0) * beer.popularity);
                write_pos_row(file, day, "Beer", beer.name + " Pint", pints, 7.0);
            }
            write_pos_row(file, day, "Food", "Pretzel", 3 + static_cast<int>(uniform(0.0, 8.0)), 9.0);
            write_pos_row(file, day, "Merch", "T-shirt", static_cast<int>(uniform(0.0, 3.0)), 25.0);
        }
    }

private:
    static app::Session start(data::Database& db) {
        data::migrate(db);
        return app::open_local_session(db);
    }

    double uniform(double low, double high) {
        return std::uniform_real_distribution<double>(low, high)(random_);
    }

    // Busier in summer: 1 in midwinter to 1.6 in mid-July.
    static double season(Date day) {
        const double year_part = (domain::days_between({day.year, 1, 1}, day) + 0.5) / 365.0;
        return 1.3 - 0.3 * std::cos((year_part - 0.04) * 2.0 * 3.14159265358979);
    }

    void set_up(Date first, Date last) {
        // The floor: a 10-barrel brewhouse, four 20-barrel fermenters, and the rooms.
        brewhouse_ = floor_.add(domain::ElementType::Brewhouse, 120.0, 120.0);
        floor_.set_capacity(brewhouse_, 10.0 * kBarrel);
        for (int i = 0; i < 4; ++i) {
            Tank tank;
            tank.id = floor_.add(domain::ElementType::Fermenter, 300.0 + 170.0 * i, 120.0);
            floor_.rename(tank.id, "FV" + std::to_string(i + 1));
            floor_.set_capacity(tank.id, 20.0 * kBarrel);
            tanks_.push_back(tank);
        }
        floor_.add(domain::ElementType::Inventory, 160.0, 380.0);
        floor_.add(domain::ElementType::PackagedGoods, 480.0, 380.0);
        floor_.add(domain::ElementType::KegCleaner, 780.0, 380.0);

        for (const Stocked& stocked : kStock) {
            domain::InventoryItem item;
            item.name = stocked.name;
            item.category = stocked.category;
            item.unit = stocked.unit;
            item.reorder_level = stocked.reorder;
            if (stocked.category == domain::ItemCategory::Can) item.fill_volume_l = kPint;
            items_[stocked.name] = inventory_.add_item(item);
        }
        for (const Stocked& stocked : kStock) order(stocked, at(first, 9));

        // New kegs.
        const std::vector<domain::KegCount> kegs{{PackageType::HalfBarrel, 70}, {PackageType::Sixtel, 50}};
        goods_.add_kegs(kegs, at(first, 9), "New kegs");
        goods_.clean_all(at(first, 10));

        // Customers and prices.
        distributor_ = sales_.add_customer({0, "Hopkins Distributing", domain::Channel::Distributor, "orders@hopkins.example",
                                            "Orders on Mondays", true});
        for (const char* name : {"The Tap House", "Corner Bottle Shop", "Riverside Grill"}) {
            accounts_.push_back(sales_.add_customer({0, name, domain::Channel::Retail, "", "", true}));
        }
        const Date from{first.year, first.month, 1};
        add_price(domain::Channel::Distributor, PackageType::HalfBarrel, 1, 160.0, from);
        add_price(domain::Channel::Distributor, PackageType::Sixtel, 1, 62.0, from);
        add_price(domain::Channel::Retail, PackageType::HalfBarrel, 1, 185.0, from);
        add_price(domain::Channel::Retail, PackageType::Sixtel, 1, 72.0, from);
        add_price(domain::Channel::ToGo, PackageType::Can, 4, 16.0, from);
        domain::PriceEntry pint;
        pint.channel = domain::Channel::Taproom;
        pint.priced_as = domain::PricedAs::Pour;
        pint.yield = 0.88;
        pint.price = 7.0 / kPint;  // $7 a pint, per liter
        pint.effective_from = from;
        sales_.add_price(pint);
        // Prices went up halfway through, at the start of a month.
        const Date middle = domain::add_days(first, domain::days_between(first, last) / 2);
        const Date rise{middle.year, middle.month, 1};
        add_price(domain::Channel::Distributor, PackageType::HalfBarrel, 1, 168.0, rise);
        add_price(domain::Channel::ToGo, PackageType::Can, 4, 17.0, rise);
        sales_.set_keg_deposits({30.0, 30.0});

        // What it costs to run.
        std::map<std::string, std::int64_t> categories;
        for (const domain::ExpenseCategory& category : expenses_.categories()) categories[category.name] = category.id;
        const domain::Month month = domain::month_of(first);
        expenses_.add_recurring({0, categories["Rent"], "Rent", 4500.0, month, std::nullopt});
        expenses_.add_recurring({0, categories["Wages"], "Head brewer", 5200.0, month, std::nullopt});
        expenses_.add_recurring({0, categories["Wages"], "Taproom staff", 6800.0, month, std::nullopt});
        expenses_.add_recurring({0, categories["Insurance"], "Liability insurance", 420.0, month, std::nullopt});
        utilities_ = categories["Utilities"];
        repairs_ = categories["Repairs and maintenance"];
        marketing_ = categories["Marketing"];
        cleaning_ = categories["Cleaning and lab supplies"];
    }

    void add_price(domain::Channel channel, PackageType package, int pack, double price, Date from) {
        domain::PriceEntry entry;
        entry.channel = channel;
        entry.package = package;
        entry.pack_size = pack;
        entry.price = price;
        entry.effective_from = from;
        sales_.add_price(entry);
    }

    // A delivery of an item, at today's price (a little more each month).
    void order(const Stocked& stocked, domain::Timestamp when) {
        ++deliveries_;
        const double drift = 1.0 + 0.004 * static_cast<double>(deliveries_ / 4);
        domain::Invoice invoice;
        invoice.received_at = when;
        invoice.supplier = stocked.supplier;
        invoice.number = "INV-" + std::to_string(1000 + deliveries_);
        invoice.lines.push_back({items_.at(stocked.name), stocked.delivery,
                                 stocked.delivery * stocked.price * drift * uniform(0.97, 1.05), "",
                                 std::nullopt});
        inventory_.receive(invoice);
    }

    double on_hand(const std::string& item) const {
        for (const app::ItemStock& stock : inventory_.stock()) {
            if (stock.item.name == item) return stock.on_hand;
        }
        return 0.0;
    }

    // Packages of a beer on hand.
    int packages(const std::string& beer, PackageType type) const {
        int count = 0;
        for (const domain::PackagedStock& stock : goods_.packaged()) {
            if (stock.beer == beer && stock.type == type) count += stock.count;
        }
        return count;
    }

    void live(Date day, bool last_day) {
        const int weekday = domain::weekday(day);  // 0 is Sunday

        // Deliveries come the morning after stock falls below its reorder
        // level; now and then one is logged a few days late, after the brew
        // that used it.
        for (const Stocked& stocked : kStock) {
            if (on_hand(stocked.name) >= stocked.reorder || pending_[stocked.name]) continue;
            pending_[stocked.name] = true;
            const int late = uniform(0.0, 1.0) < 0.15 ? 3 : 0;
            deliveries_due_.push_back({domain::add_days(day, 1 + late), stocked.name});
        }
        for (auto due = deliveries_due_.begin(); due != deliveries_due_.end();) {
            if (due->first != day) {
                ++due;
                continue;
            }
            const auto stocked = std::find_if(kStock.begin(), kStock.end(),
                                              [&](const Stocked& s) { return s.name == due->second; });
            order(*stocked, at(day, 8));
            pending_[due->second] = false;
            due = deliveries_due_.erase(due);
        }

        // Package what's ready, and clean the tank for the next brew.
        for (Tank& tank : tanks_) {
            if (!tank.full || day < tank.ready) continue;
            package(tank, day);
        }
        // Brew every nine days, the next beer in turn, into an empty tank.
        if (!last_day && domain::days_between(first_brew_, day) % 9 == 0) brew(day);

        // A distributor's order on Mondays, accounts mid-week, cans to go on
        // weekends, and kegs used up in the taproom most days.
        const double busy = season(day);
        if (weekday == 1) distributor_order(day, busy);
        if (weekday == 3) account_order(day, busy);
        if (weekday == 5 || weekday == 6) to_go(day, busy);
        if (weekday != 1 && uniform(0.0, 1.0) < 0.35 * busy) taproom_keg(day);
        if (weekday == 4) return_kegs(day);
        if (weekday == 2) goods_.clean_all(at(day, 16));

        // Bills.
        if (day.day == 12) {
            expenses_.add_expense({0, day, "City Power & Water", utilities_, std::round(700.0 * busy * uniform(0.9, 1.1)),
                                   "", ""});
        }
        if (day.day == 20) {
            expenses_.add_expense({0, day, "Brewery Supply Co", cleaning_, std::round(uniform(150.0, 320.0)), "", ""});
        }
        if (day.day == 5 && uniform(0.0, 1.0) < 0.35) {
            expenses_.add_expense({0, day, "Ace Mechanical", repairs_, std::round(uniform(200.0, 1400.0)), "",
                                   "Glycol and pumps"});
        }
        if (day.day == 1 && day.month % 3 == 0) {
            expenses_.add_expense({0, day, "Local Print & Sign", marketing_, std::round(uniform(300.0, 900.0)), "", ""});
        }
    }

    void brew(Date day) {
        const auto empty = std::find_if(tanks_.begin(), tanks_.end(), [](const Tank& tank) { return !tank.full; });
        if (empty == tanks_.end()) return;
        const std::size_t which = next_beer_++ % kBeers.size();
        const Beer& beer = kBeers[which];
        domain::Recipe recipe;
        recipe.name = beer.name;
        recipe.style = beer.style;
        app::KnockOut knock_out;
        knock_out.vessel_id = empty->id;
        knock_out.at = at(day, 15);
        knock_out.volume_l = 10.0 * kBarrel * uniform(0.96, 1.02);
        knock_out.og = beer.name == "Oatmeal Stout" ? 1.058 : beer.name == "Pils" ? 1.048 : 1.054;
        for (const auto& [name, amount] : beer.ingredients) {
            domain::IngredientUse use;
            use.name = name;
            const auto stocked = std::find_if(kStock.begin(), kStock.end(), [&](const Stocked& s) { return s.name == name; });
            const domain::ItemCategory category = stocked->category;
            use.amount_kind = stocked->unit == domain::StockUnit::Count ? domain::AmountKind::Count : domain::AmountKind::Mass;
            use.kind = category == domain::ItemCategory::Hop     ? domain::IngredientKind::Hop
                       : category == domain::ItemCategory::Yeast ? domain::IngredientKind::Yeast
                                                                  : domain::IngredientKind::Fermentable;
            use.planned = amount;
            use.actual = amount * uniform(0.98, 1.02);
            knock_out.ingredients.push_back(use);
        }
        batches_.log_brew({.brewhouse_id = brewhouse_, .at = at(day, 7), .code = std::to_string(100 + next_beer_),
                           .recipe = recipe},
                          knock_out);
        empty->full = true;
        empty->beer = which;
        empty->ready = domain::add_days(day, beer.tank_days + static_cast<int>(uniform(0.0, 4.0)));
    }

    void package(Tank& tank, Date day) {
        const double volume = batches_.contents(tank.id) ? batches_.contents(tank.id)->volume_l : 0.0;
        // About 60 % in half-barrels, 20 % in sixtels, the rest in cans.
        app::Packaging packaging;
        packaging.vessel_id = tank.id;
        packaging.at = at(day, 11);
        const double usable = volume * uniform(0.93, 0.97);  // the rest is left behind
        packaging.half_barrels = static_cast<int>(usable * 0.6 / domain::keg_volume_l(PackageType::HalfBarrel));
        packaging.sixtels = static_cast<int>(usable * 0.2 / domain::keg_volume_l(PackageType::Sixtel));
        packaging.cans = static_cast<int>(usable * 0.19 / kPint / 24.0) * 24;
        packaging.can_item_id = items_.at("16 oz cans");
        batches_.package(packaging);
        batches_.set_condition(tank.id, domain::VesselCondition::Clean, at(day, 14));
        tank.full = false;
    }

    // Sells up to `wanted` packages of each beer, as far as there are.
    void sell(Date day, int hour, domain::Channel channel, std::optional<std::int64_t> customer, PackageType type,
              int pack, double wanted, bool deposits) {
        domain::Sale sale;
        sale.at = at(day, hour);
        sale.channel = channel;
        sale.customer_id = customer;
        for (const Beer& beer : kBeers) {
            const int units = std::min(static_cast<int>(std::round(wanted * beer.popularity * uniform(0.6, 1.4))),
                                       packages(beer.name, type) / pack);
            if (units <= 0) continue;
            domain::SaleLine line;
            line.beer = beer.name;
            line.package = type;
            line.unit_volume_l = type == PackageType::Can ? kPint : domain::keg_volume_l(type);
            line.quantity = units;
            line.pack_size = pack;
            sale.lines.push_back(line);
        }
        if (sale.lines.empty()) return;
        sale.number = channel == domain::Channel::ToGo ? "" : std::to_string(5000 + ++invoices_);
        sales_.record_sale(sale, deposits);
    }

    void distributor_order(Date day, double busy) {
        sell(day, 10, domain::Channel::Distributor, distributor_, PackageType::HalfBarrel, 1, 1.2 * busy, true);
        sell(day, 10, domain::Channel::Distributor, distributor_, PackageType::Sixtel, 1, 1.0 * busy, true);
    }

    void account_order(Date day, double busy) {
        const std::int64_t account = accounts_[static_cast<std::size_t>(uniform(0.0, 2.999))];
        sell(day, 13, domain::Channel::Retail, account, PackageType::Sixtel, 1, 0.8 * busy, true);
        if (uniform(0.0, 1.0) < 0.5) sell(day, 13, domain::Channel::Retail, account, PackageType::HalfBarrel, 1, 0.6, true);
    }

    void to_go(Date day, double busy) {
        sell(day, 17, domain::Channel::ToGo, std::nullopt, PackageType::Can, 4, 9.0 * busy, false);
    }

    void taproom_keg(Date day) {
        // The beer the taproom is shortest of, of those on hand.
        std::vector<const Beer*> on_tap;
        for (const Beer& beer : kBeers) {
            if (packages(beer.name, PackageType::HalfBarrel) > 0) on_tap.push_back(&beer);
        }
        if (on_tap.empty()) return;
        const Beer& beer = *on_tap[static_cast<std::size_t>(uniform(0.0, static_cast<double>(on_tap.size()) - 0.001))];
        sales_.take_to_taproom({.beer = beer.name, .type = PackageType::HalfBarrel,
                                .unit_volume_l = domain::keg_volume_l(PackageType::HalfBarrel), .count = 1,
                                .at = at(day, 18)});
    }

    void return_kegs(Date day) {
        // Most kegs out come back within a couple of weeks.
        std::vector<std::int64_t> customers = accounts_;
        customers.push_back(distributor_);
        for (std::int64_t customer : customers) {
            std::vector<domain::KegCount> back;
            for (PackageType type : domain::kKegTypes) {
                const int out = sales_.keg_account(customer, type).out();
                const int count = static_cast<int>(out * uniform(0.3, 0.7));
                if (count > 0) back.push_back({type, count});
            }
            if (!back.empty()) sales_.return_kegs(customer, back, at(day, 9), "");
        }
    }

    void write_pos_row(std::ofstream& file, Date day, const char* category, const std::string& item, int count,
                       double price) {
        if (count <= 0) return;
        char line[256];
        std::snprintf(line, sizeof line, "%02d/%02d/%04d,%02d:%02d,%s,%s,%d,$%.2f,$0.00,$%.2f,$%.2f\r\n", day.month,
                      day.day, day.year, 17 + count % 5, (count * 7) % 60, category, item.c_str(), count, count * price,
                      count * price, count * price * 0.08);
        file << line;
    }

    data::Database db_;
    app::Session session_;
    data::SettingsRepository settings_;
    app::FloorService floor_;
    app::InventoryService inventory_;
    app::PackagedGoodsService goods_;
    app::BatchService batches_;
    app::SalesService sales_;
    app::ExpenseService expenses_;
    std::mt19937 random_;

    std::int64_t brewhouse_ = 0;
    std::vector<Tank> tanks_;
    std::map<std::string, std::int64_t> items_;
    std::map<std::string, bool> pending_;
    std::vector<std::pair<Date, std::string>> deliveries_due_;
    int deliveries_ = 0;
    std::int64_t distributor_ = 0;
    std::vector<std::int64_t> accounts_;
    std::size_t next_beer_ = 0;
    Date first_brew_{};
    int invoices_ = 0;
    std::int64_t utilities_ = 0;
    std::int64_t repairs_ = 0;
    std::int64_t marketing_ = 0;
    std::int64_t cleaning_ = 0;
};

} // namespace

int main(int argc, char** argv) {
    const int months = argc == 3 ? std::atoi(argv[2]) : 12;
    if (argc < 2 || argc > 3 || months < 1 || months > 60) {
        std::fprintf(stderr, "Usage: ninkasi_demo <new database file> [months, 1 to 60; 12 if left out]\n");
        return 2;
    }
    const std::filesystem::path path = argv[1];
    if (std::filesystem::exists(path)) {
        std::fprintf(stderr, "%s already exists: give a new file.\n", path.string().c_str());
        return 2;
    }
    try {
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
        const Date last = domain::today();
        const Date first = domain::add_days(last, -static_cast<int>(std::lround(months * 365.25 / 12.0)) + 1);
        DemoBrewery brewery(path);
        brewery.make(first, last);
        const std::filesystem::path pos = path.parent_path() / "pos-export.csv";
        brewery.write_pos_export(pos, last);
        std::printf("Made %s (%s to %s) and %s.\n", path.string().c_str(), domain::to_iso8601(first).c_str(),
                    domain::to_iso8601(last).c_str(), pos.string().c_str());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Couldn't make the demo brewery: %s\n", e.what());
        return 1;
    }
}
