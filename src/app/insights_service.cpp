#include "app/insights_service.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <utility>

namespace ninkasi::app {

namespace {

constexpr const char* kTankDaysKey = "insights.tank_days";
constexpr const char* kRateWeeksKey = "insights.rate_weeks";
constexpr int kDefaultTankDays = 21;
constexpr int kDefaultRateWeeks = 8;

// Beers are the same whatever their case: the key they're gathered under.
std::string beer_key(const std::string& beer) {
    std::string key = beer;
    for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return key;
}

bool in(domain::Date from, domain::Date to, domain::Timestamp at) {
    const domain::Date day = domain::local_date(at);
    return from <= day && day <= to;
}

// Liters of a packaged goods movement: positive in, negative out.
double liters(const domain::PackagedMovement& movement) {
    return movement.quantity * movement.unit_volume_l;
}

// Whether a stock movement is stock used: in a brew, at packaging, or otherwise.
bool is_use(const domain::StockMovement& movement) {
    if (!(movement.quantity < 0.0)) return false;
    switch (movement.reason) {
    case domain::StockReason::Brewed:
    case domain::StockReason::Packaged:
    case domain::StockReason::Used:
    case domain::StockReason::Discarded: return true;
    default:                             return false;
    }
}

int setting(data::SettingsRepository& settings, const char* key, int fallback) {
    try {
        return std::max(1, std::stoi(settings.get_or(key, std::to_string(fallback))));
    } catch (const std::exception&) {
        return fallback;
    }
}

int urgency(domain::RunwayStatus status) {
    switch (status) {
    case domain::RunwayStatus::Out:        return 0;
    case domain::RunwayStatus::BrewNow:    return 1;
    case domain::RunwayStatus::BrewSoon:   return 2;
    case domain::RunwayStatus::Plenty:     return 3;
    case domain::RunwayStatus::NotSelling: return 4;
    }
    return 4;
}

} // namespace

std::optional<double> Figure::change() const {
    if (!before) return std::nullopt;
    return domain::change(now, *before);
}

std::optional<double> BatchCostRow::margin() const {
    if (!(revenue > 0.0)) return std::nullopt;
    return (revenue - cost_sold) / revenue;
}

std::optional<double> ProfitLossColumn::gross_margin() const {
    if (!(total_revenue > 0.0)) return std::nullopt;
    return gross_profit / total_revenue;
}

std::optional<double> ProfitLoss::BeerMargin::margin() const {
    if (!(revenue > 0.0)) return std::nullopt;
    return (revenue - cost) / revenue;
}

InsightsService::InsightsService(data::Database& db, Session session, data::SettingsRepository& settings,
                                 TtbService& ttb, ExpenseService& expenses, CostingService& costing,
                                 std::function<std::uint64_t()> stamp)
    : db_(db), session_(std::move(session)), settings_(settings), ttb_(ttb), expenses_(expenses), costing_(costing),
      stamp_(std::move(stamp)) {}

void InsightsService::refresh() {
    const std::uint64_t stamp = stamp_();
    if (loaded_stamp_ == stamp) return;
    data_ = data::AnalyticsRepository(db_).load();
    sale_of_line_.clear();
    actual_days_.clear();
    for (const domain::Sale& sale : data_.sales) {
        for (const domain::SaleLine& line : sale.lines) sale_of_line_[line.id] = &sale;
        if (sale.source == domain::SaleSource::Pos || sale.source == domain::SaleSource::Takings) {
            actual_days_.insert(domain::local_date(sale.at));
        }
    }
    if (money()) costing_.refresh(stamp);
    loaded_stamp_ = stamp;
    ++revision_;
}

int InsightsService::default_tank_days() const {
    return setting(settings_, kTankDaysKey, kDefaultTankDays);
}

int InsightsService::rate_weeks() const {
    return setting(settings_, kRateWeeksKey, kDefaultRateWeeks);
}

void InsightsService::set_planning(int default_tank_days, int rate_weeks) {
    require_admin(session_, "change how brewing is planned");
    if (default_tank_days < 1 || rate_weeks < 1) throw std::invalid_argument("Use at least a day and a week.");
    settings_.set(kTankDaysKey, std::to_string(default_tank_days));
    settings_.set(kRateWeeksKey, std::to_string(rate_weeks));
    loaded_stamp_.reset();  // work the figures out again
}

// --- Shared figures ---------------------------------------------------------------

const domain::BatchFacts* InsightsService::batch(std::int64_t id) const {
    for (const domain::BatchFacts& facts : data_.ledgers.batches) {
        if (facts.id == id) return &facts;
    }
    return nullptr;
}

std::map<std::string, double> InsightsService::sold_by_beer(domain::Date from, domain::Date to) const {
    std::map<std::string, double> sold;
    for (const domain::PackagedMovement& movement : data_.ledgers.packaged) {
        if (!domain::counts_as_sold(movement.reason) || !in(from, to, movement.at)) continue;
        const domain::BatchFacts* facts = batch(movement.batch_id);
        if (facts) sold[facts->beer] -= liters(movement);
    }
    return sold;
}

double InsightsService::sold_l(domain::Date from, domain::Date to) const {
    double total = 0.0;
    for (const auto& [beer, volume] : sold_by_beer(from, to)) total += volume;
    return total;
}

double InsightsService::brewed_l(domain::Date from, domain::Date to) const {
    double total = 0.0;
    for (const data::BrewFact& brew : data_.brews) {
        if (brew.knocked_out_at && in(from, to, *brew.knocked_out_at)) total += brew.volume_l;
    }
    return total;
}

double InsightsService::counted(const domain::Sale& sale, const domain::SaleLine& line) const {
    if (domain::is_expected(sale, line) && actual_days_.count(domain::local_date(sale.at))) return 0.0;
    return line.amount();
}

ExpectedRevenue InsightsService::expected_between(domain::Date from, domain::Date to) const {
    ExpectedRevenue expected;
    std::set<domain::Date> days;
    for (const domain::Sale& sale : data_.sales) {
        if (!in(from, to, sale.at)) continue;
        for (const domain::SaleLine& line : sale.lines) {
            if (!domain::is_expected(sale, line) || actual_days_.count(domain::local_date(sale.at))) continue;
            expected.amount += line.amount();
            days.insert(domain::local_date(sale.at));
        }
    }
    expected.days = static_cast<int>(days.size());
    return expected;
}

double InsightsService::revenue(domain::Date from, domain::Date to) const {
    double total = 0.0;
    for (const domain::Sale& sale : data_.sales) {
        if (!in(from, to, sale.at)) continue;
        for (const domain::SaleLine& line : sale.lines) {
            if (line.kind != domain::LineKind::Deposit) total += counted(sale, line);
        }
    }
    return total;
}

Figure InsightsService::figure(const InsightsQuery& query, const std::vector<domain::Bucket>& buckets,
                               const std::function<double(domain::Date, domain::Date)>& over) const {
    Figure result;
    result.now = over(query.period.from, query.period.to);
    if (const std::optional<domain::Period> compared = domain::comparison_period(query.period, query.comparison)) {
        result.before = over(compared->from, compared->to);
    }
    for (const domain::Bucket& bucket : buckets) result.series.push_back(over(bucket.first, bucket.last));
    return result;
}

std::map<std::string, std::vector<int>> InsightsService::tank_days_by_beer() const {
    // Each batch's first brew and first packaging.
    std::map<std::int64_t, domain::Timestamp> brewed;
    for (const data::BrewFact& brew : data_.brews) {
        const auto found = brewed.find(brew.batch_id);
        if (found == brewed.end() || brew.started_at < found->second) brewed[brew.batch_id] = brew.started_at;
    }
    std::map<std::int64_t, domain::Timestamp> packaged;
    for (const domain::PackagedMovement& movement : data_.ledgers.packaged) {
        if (movement.reason != domain::PackagedReason::Packaged) continue;
        const auto found = packaged.find(movement.batch_id);
        if (found == packaged.end() || movement.at < found->second) packaged[movement.batch_id] = movement.at;
    }
    // By beer, oldest batch first.
    std::vector<std::pair<domain::Timestamp, std::int64_t>> order;
    for (const auto& [id, at] : brewed) {
        if (packaged.count(id)) order.emplace_back(at, id);
    }
    std::sort(order.begin(), order.end());
    std::map<std::string, std::vector<int>> days;
    for (const auto& [at, id] : order) {
        const domain::BatchFacts* facts = batch(id);
        if (!facts) continue;
        days[beer_key(facts->beer)].push_back(
            domain::days_between(domain::local_date(at), domain::local_date(packaged.at(id))));
    }
    return days;
}

// --- Overview ---------------------------------------------------------------------

Overview InsightsService::overview(const InsightsQuery& query) const {
    Overview overview;
    overview.buckets = domain::buckets(query.period, query.grouping);
    overview.sold_l = figure(query, overview.buckets, [this](domain::Date a, domain::Date b) { return sold_l(a, b); });
    overview.brewed_l =
        figure(query, overview.buckets, [this](domain::Date a, domain::Date b) { return brewed_l(a, b); });
    overview.kegs_out = data_.kegs_out;
    for (const BeerRunway& beer : stock(domain::today())) {
        const domain::RunwayStatus status = beer.runway.status;
        if (status == domain::RunwayStatus::BrewNow || status == domain::RunwayStatus::BrewSoon ||
            status == domain::RunwayStatus::Out) {
            ++overview.beers_to_brew;
        }
    }
    if (!money()) return overview;

    overview.revenue = figure(query, overview.buckets, [this](domain::Date a, domain::Date b) { return revenue(a, b); });
    overview.gross_margin = figure(query, overview.buckets, [this](domain::Date a, domain::Date b) {
        const double earned = revenue(a, b);
        if (!(earned > 0.0)) return 0.0;
        return (earned - costing_.period_costs(domain::start_of(a), domain::end_of(b)).sold) / earned;
    });
    overview.inventory_value = costing_.inventory_value().total();
    overview.expected = expected_between(query.period.from, query.period.to);
    return overview;
}

// --- Sales ------------------------------------------------------------------------

SalesInsights InsightsService::sales(const InsightsQuery& query) const {
    SalesInsights insights;
    insights.buckets = domain::buckets(query.period, query.grouping);
    const std::size_t n = insights.buckets.size();
    std::map<domain::Channel, ChannelSeries> channels;
    const auto channel_series = [&](domain::Channel channel) -> ChannelSeries& {
        ChannelSeries& series = channels[channel];
        series.channel = channel;
        series.revenue.resize(n);
        series.volume_l.resize(n);
        return series;
    };

    // Volume, from the beer that left, under the channel of the sale it left for.
    std::map<std::string, BeerSales> beers;
    const auto beer_sales = [&](const std::string& beer) -> BeerSales& {
        BeerSales& entry = beers[beer_key(beer)];
        if (entry.beer.empty()) entry.beer = beer;
        return entry;
    };
    const std::optional<domain::Period> compared = domain::comparison_period(query.period, query.comparison);
    for (const domain::PackagedMovement& movement : data_.ledgers.packaged) {
        if (!domain::counts_as_sold(movement.reason)) continue;
        const domain::BatchFacts* facts = batch(movement.batch_id);
        if (!facts) continue;
        const double volume = -liters(movement);
        BeerSales& entry = beer_sales(facts->beer);
        entry.lifetime_l += volume;
        const domain::Date day = domain::local_date(movement.at);
        if (compared && compared->contains(day)) entry.volume_before = entry.volume_before.value_or(0.0) + volume;
        const std::optional<std::size_t> bucket = domain::bucket_index(insights.buckets, day);
        if (!bucket) continue;
        entry.volume_l += volume;
        entry.units[movement.type] -= movement.quantity;
        domain::Channel channel = movement.reason == domain::PackagedReason::Taproom ? domain::Channel::Taproom
                                                                                      : domain::Channel::Other;
        if (movement.sale_line_id) {
            const auto sale = sale_of_line_.find(*movement.sale_line_id);
            if (sale != sale_of_line_.end()) channel = sale->second->channel;
        }
        channel_series(channel).volume_l[*bucket] += volume;
    }

    // Revenue, from the sales recorded: the taproom's expected revenue apart,
    // and left out on days with its actual sales.
    ChannelSeries expected_series;
    expected_series.channel = domain::Channel::Taproom;
    expected_series.expected = true;
    expected_series.revenue.resize(n);
    expected_series.volume_l.resize(n);
    if (money()) {
        TaproomCheck check;
        check.expected.resize(n);
        check.actual.resize(n);
        for (const domain::Sale& sale : data_.sales) {
            const domain::Date day = domain::local_date(sale.at);
            const std::optional<std::size_t> bucket = domain::bucket_index(insights.buckets, day);
            const bool actual_day = actual_days_.count(day) > 0;
            for (const domain::SaleLine& line : sale.lines) {
                if (line.kind == domain::LineKind::Deposit) continue;
                const double amount = counted(sale, line);
                const bool expected = domain::is_expected(sale, line);
                if (!line.beer.empty()) {
                    BeerSales& entry = beer_sales(line.beer);
                    entry.lifetime_revenue += amount;
                    if (bucket) entry.revenue += amount;
                }
                if (!bucket) continue;
                (expected ? expected_series : channel_series(sale.channel)).revenue[*bucket] += amount;
                if (!line.unit_price) ++insights.unpriced_lines;
                // Expected against actual: the kegs sent, and the point of sale's beer.
                if (expected && actual_day) {
                    check.expected[*bucket] += line.amount();
                    check.expected_on_those_days += line.amount();
                } else if (sale.source == domain::SaleSource::Pos && line.kind == domain::LineKind::Pour) {
                    check.actual[*bucket] += line.amount();
                    check.actual_on_those_days += line.amount();
                }
            }
        }
        for (domain::Date day : actual_days_) {
            if (query.period.contains(day)) ++check.actual_days;
        }
        check.counted = expected_between(query.period.from, query.period.to);
        insights.taproom = std::move(check);
    }

    for (domain::Channel channel : domain::kChannels) {
        const auto found = channels.find(channel);
        if (found != channels.end()) insights.channels.push_back(found->second);
        const bool any_expected = std::any_of(expected_series.revenue.begin(), expected_series.revenue.end(),
                                              [](double value) { return value != 0.0; });
        if (channel == domain::Channel::Taproom && any_expected) insights.channels.push_back(expected_series);
    }
    for (auto& [key, entry] : beers) {
        if (entry.volume_l == 0.0 && entry.revenue == 0.0 && !entry.volume_before) continue;
        entry.per_week_l = domain::per_week(entry.volume_l, query.period.days());
        insights.beers.push_back(std::move(entry));
    }
    std::sort(insights.beers.begin(), insights.beers.end(),
              [](const BeerSales& a, const BeerSales& b) { return a.volume_l > b.volume_l; });
    return insights;
}

BeerDetail InsightsService::beer_detail(const std::string& beer, const InsightsQuery& query) const {
    BeerDetail detail;
    detail.beer = beer;
    const std::string key = beer_key(beer);

    // Every month from its first sale.
    std::optional<domain::Date> first;
    for (const domain::PackagedMovement& movement : data_.ledgers.packaged) {
        const domain::BatchFacts* facts = batch(movement.batch_id);
        if (!domain::counts_as_sold(movement.reason) || !facts || beer_key(facts->beer) != key) continue;
        const domain::Date day = domain::local_date(movement.at);
        if (!first || day < *first) first = day;
    }
    if (first) {
        detail.months = domain::buckets({*first, std::max(*first, query.period.to)}, domain::Grouping::Month);
        detail.monthly_l.resize(detail.months.size());
    }

    std::map<std::string, double> by_batch;
    for (const domain::PackagedMovement& movement : data_.ledgers.packaged) {
        const domain::BatchFacts* facts = batch(movement.batch_id);
        if (!domain::counts_as_sold(movement.reason) || !facts || beer_key(facts->beer) != key) continue;
        const domain::Date day = domain::local_date(movement.at);
        if (const std::optional<std::size_t> month = domain::bucket_index(detail.months, day)) {
            detail.monthly_l[*month] -= liters(movement);
        }
        if (query.period.contains(day)) by_batch[facts->code] -= liters(movement);
    }
    for (const auto& [code, volume] : by_batch) detail.batches.push_back({code, volume});

    // Who bought it in the period, from the sales recorded.
    std::map<std::string, BeerDetail::Customer> customers;
    for (const domain::Sale& sale : data_.sales) {
        if (!query.period.contains(domain::local_date(sale.at))) continue;
        const std::string name = sale.customer.empty() ? std::string(domain::name(sale.channel)) : sale.customer;
        for (const domain::SaleLine& line : sale.lines) {
            if (line.beer.empty() || beer_key(line.beer) != key) continue;
            BeerDetail::Customer& customer = customers[name];
            customer.name = name;
            customer.volume_l += line.volume_l();
            if (money()) customer.revenue += counted(sale, line);
        }
    }
    for (auto& [name, customer] : customers) detail.customers.push_back(std::move(customer));
    std::sort(detail.customers.begin(), detail.customers.end(),
              [](const auto& a, const auto& b) { return a.volume_l > b.volume_l; });
    return detail;
}

std::vector<domain::Sale> InsightsService::sales_between(domain::Date from, domain::Date to) const {
    std::vector<domain::Sale> found;
    for (const domain::Sale& sale : data_.sales) {
        if (in(from, to, sale.at)) found.push_back(sale);
    }
    return found;  // the sales are read newest first
}

// --- Stock and planning -----------------------------------------------------------

std::vector<BeerRunway> InsightsService::stock(domain::Date today) const {
    const int window_days = rate_weeks() * 7;
    const std::map<std::string, double> sold = sold_by_beer(domain::add_days(today, -(window_days - 1)), today);
    const std::map<std::string, std::vector<int>> tank_days = tank_days_by_beer();

    std::map<std::string, BeerRunway> beers;
    const auto entry = [&](const std::string& beer) -> BeerRunway& {
        BeerRunway& runway = beers[beer_key(beer)];
        if (runway.beer.empty()) runway.beer = beer;
        return runway;
    };
    for (const domain::PackagedMovement& movement : data_.ledgers.packaged) {
        if (const domain::BatchFacts* facts = batch(movement.batch_id)) entry(facts->beer).packaged_l += liters(movement);
    }
    for (const domain::VolumeEvent& event : data_.volume_events) {
        const domain::BatchFacts* facts = batch(event.batch_id);
        if (facts && domain::is_active(facts->status)) entry(facts->beer).in_tanks_l += event.volume_l;
    }
    std::map<std::string, double> sold_by_key;
    for (const auto& [beer, volume] : sold) sold_by_key[beer_key(beer)] += volume;
    for (const auto& [beer, volume] : sold) entry(beer);

    std::vector<BeerRunway> result;
    for (auto& [key, runway] : beers) {
        runway.packaged_l = std::max(runway.packaged_l, 0.0);
        runway.in_tanks_l = std::max(runway.in_tanks_l, 0.0);
        const auto sold_found = sold_by_key.find(key);
        const double sold_l = sold_found == sold_by_key.end() ? 0.0 : sold_found->second;
        if (runway.packaged_l + runway.in_tanks_l < domain::kEmptyVolumeL && !(sold_l > 0.0)) continue;
        const auto days = tank_days.find(key);
        runway.tank_days =
            domain::tank_days(days == tank_days.end() ? std::span<const int>() : std::span<const int>(days->second),
                              default_tank_days());
        runway.runway = domain::runway(runway.packaged_l + runway.in_tanks_l, domain::per_week(sold_l, window_days),
                                       today, runway.tank_days);
        result.push_back(std::move(runway));
    }
    std::sort(result.begin(), result.end(), [](const BeerRunway& a, const BeerRunway& b) {
        const int ua = urgency(a.runway.status);
        const int ub = urgency(b.runway.status);
        if (ua != ub) return ua < ub;
        return a.runway.days_left.value_or(1 << 30) < b.runway.days_left.value_or(1 << 30);
    });
    return result;
}

// --- Ingredients ------------------------------------------------------------------

IngredientInsights InsightsService::ingredients(const InsightsQuery& query) const {
    IngredientInsights insights;
    insights.buckets = domain::buckets(query.period, query.grouping);
    const domain::Date today = domain::today();
    const int window_days = rate_weeks() * 7;
    const domain::Date window_from = domain::add_days(today, -(window_days - 1));

    std::map<std::int64_t, ItemUse> items;
    for (const domain::InventoryItem& item : data_.items) {
        ItemUse& use = items[item.id];
        use.item = item;
        use.series.resize(insights.buckets.size());
    }
    std::map<std::int64_t, double> recent;
    for (const domain::StockMovement& movement : data_.ledgers.movements) {
        const auto found = items.find(movement.item_id);
        if (found == items.end()) continue;
        ItemUse& use = found->second;
        use.on_hand += movement.quantity;
        if (!is_use(movement)) continue;
        const domain::Date day = domain::local_date(movement.at);
        if (window_from <= day && day <= today) recent[movement.item_id] -= movement.quantity;
        if (const std::optional<std::size_t> bucket = domain::bucket_index(insights.buckets, day)) {
            use.used -= movement.quantity;
            use.series[*bucket] -= movement.quantity;
        }
    }
    for (auto& [id, use] : items) {
        if (use.item.archived && use.used == 0.0) continue;
        if (use.used == 0.0 && !(use.on_hand > 0.0)) continue;
        use.per_week = domain::per_week(recent[id], window_days);
        if (use.per_week > 0.0) {
            const auto days_until = [&](double above) {
                return static_cast<int>(std::floor(std::max(use.on_hand - above, 0.0) / use.per_week * 7.0));
            };
            use.days_left = days_until(0.0);
            if (use.item.reorder_level) use.days_to_reorder = days_until(*use.item.reorder_level);
        }
        insights.items.push_back(std::move(use));
    }
    std::sort(insights.items.begin(), insights.items.end(),
              [](const ItemUse& a, const ItemUse& b) { return a.used > b.used; });
    if (!money()) return insights;

    // What each lot cost a unit, and who was paid for those received in the period.
    std::map<std::string, double> suppliers;
    for (const domain::CostedLot& lot : data_.ledgers.lots) {
        const std::optional<double> unit = domain::unit_cost(lot);
        if (!unit) continue;
        insights.prices[lot.item_id].push_back({domain::local_date(lot.received_at), *unit, lot.supplier});
        if (in(query.period.from, query.period.to, lot.received_at)) {
            suppliers[lot.supplier.empty() ? "No supplier given" : lot.supplier] += *lot.cost;
        }
    }
    for (auto& [item, points] : insights.prices) {
        std::sort(points.begin(), points.end(), [](const PricePoint& a, const PricePoint& b) { return a.day < b.day; });
    }
    insights.suppliers.assign(suppliers.begin(), suppliers.end());
    std::sort(insights.suppliers.begin(), insights.suppliers.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    insights.stock_value = costing_.inventory_value().stock;
    return insights;
}

// --- Production -------------------------------------------------------------------

ProductionInsights InsightsService::production(const InsightsQuery& query) const {
    ProductionInsights insights;
    insights.buckets = domain::buckets(query.period, query.grouping);
    insights.brewed_l =
        figure(query, insights.buckets, [this](domain::Date a, domain::Date b) { return brewed_l(a, b); });
    insights.packaged_l = figure(query, insights.buckets, [this](domain::Date a, domain::Date b) {
        double total = 0.0;
        for (const domain::PackagedMovement& movement : data_.ledgers.packaged) {
            if (movement.reason == domain::PackagedReason::Packaged && in(a, b, movement.at)) total += liters(movement);
        }
        return total;
    });

    // The batches finished in the period, and where their beer went.
    std::map<std::int64_t, int> days_in_tanks;
    {
        std::map<std::int64_t, domain::Timestamp> brewed;
        for (const data::BrewFact& brew : data_.brews) {
            const auto found = brewed.find(brew.batch_id);
            if (found == brewed.end() || brew.started_at < found->second) brewed[brew.batch_id] = brew.started_at;
        }
        std::map<std::int64_t, domain::Timestamp> packaged;
        for (const domain::PackagedMovement& movement : data_.ledgers.packaged) {
            if (movement.reason != domain::PackagedReason::Packaged) continue;
            const auto found = packaged.find(movement.batch_id);
            if (found == packaged.end() || movement.at < found->second) packaged[movement.batch_id] = movement.at;
        }
        for (const auto& [id, at] : brewed) {
            const auto done = packaged.find(id);
            if (done != packaged.end()) {
                days_in_tanks[id] = domain::days_between(domain::local_date(at), domain::local_date(done->second));
            }
        }
    }
    for (const domain::BatchFacts& facts : data_.ledgers.batches) {
        if (domain::is_active(facts.status) || !facts.finished_at || !in(query.period.from, query.period.to, *facts.finished_at)) {
            continue;
        }
        BatchLosses losses;
        losses.batch_id = facts.id;
        losses.code = facts.code;
        losses.beer = facts.beer;
        losses.brewed_l = facts.brewed_l;
        for (const domain::PackagedMovement& movement : data_.ledgers.packaged) {
            if (movement.batch_id == facts.id && movement.reason == domain::PackagedReason::Packaged) {
                losses.packaged_l += liters(movement);
            }
        }
        for (const domain::VolumeEvent& event : data_.volume_events) {
            if (event.batch_id != facts.id) continue;
            switch (event.reason) {
            case domain::VolumeReason::Loss:     losses.transfer_loss_l -= event.volume_l; break;
            case domain::VolumeReason::Dumped:   losses.dumped_l -= event.volume_l; break;
            case domain::VolumeReason::Adjusted: losses.adjusted_l -= event.volume_l; break;
            default:                             break;
            }
        }
        if (const auto days = days_in_tanks.find(facts.id); days != days_in_tanks.end()) {
            losses.days_in_tanks = days->second;
        }
        insights.batches.push_back(std::move(losses));
    }
    std::sort(insights.batches.begin(), insights.batches.end(),
              [](const BatchLosses& a, const BatchLosses& b) { return a.batch_id > b.batch_id; });

    // How much of each bucket every vessel held beer.
    const domain::Timestamp now = domain::now();
    for (const data::VesselFact& vessel : data_.vessels) {
        std::vector<domain::VolumeEvent> events;
        for (const domain::VolumeEvent& event : data_.volume_events) {
            if (event.vessel_id == vessel.id) events.push_back(event);
        }
        if (events.empty()) continue;
        const std::vector<domain::Occupancy> spans = domain::occupancy(events, now);
        insights.tanks.push_back({vessel.name, domain::occupied_share(spans, insights.buckets)});
    }

    // How long each beer stays in the tanks.
    for (const auto& [key, days] : tank_days_by_beer()) {
        std::string name = key;
        for (const domain::BatchFacts& facts : data_.ledgers.batches) {
            if (beer_key(facts.beer) == key) {
                name = facts.beer;
                break;
            }
        }
        std::vector<double> values(days.begin(), days.end());
        insights.tank_times.push_back(
            {name, static_cast<int>(std::lround(domain::median(values))), static_cast<int>(days.size())});
    }
    return insights;
}

// --- Batch costs ------------------------------------------------------------------

std::vector<BatchCostRow> InsightsService::batch_costs() const {
    require_admin(session_, "see what the beer cost");
    std::map<std::int64_t, BatchCostRow> rows;
    for (const domain::BatchFacts& facts : data_.ledgers.batches) {
        const auto cost = costing_.batches().find(facts.id);
        if (cost == costing_.batches().end() || (cost->second.draws == 0 && cost->second.packaged_l == 0.0)) continue;
        rows[facts.id] = {facts, cost->second};
    }
    // Revenue and cost of each batch's beer sold on recorded sales: a line's
    // revenue shared between its batches by the packages each gave.
    for (const domain::PackagedMovement& movement : data_.ledgers.packaged) {
        if (!movement.sale_line_id) continue;
        const auto row = rows.find(movement.batch_id);
        const auto sale = sale_of_line_.find(*movement.sale_line_id);
        if (row == rows.end() || sale == sale_of_line_.end()) continue;
        for (const domain::SaleLine& line : sale->second->lines) {
            if (line.id != *movement.sale_line_id || line.packages() == 0) continue;
            row->second.revenue += line.amount() * (-movement.quantity) / line.packages();
        }
        if (const std::optional<double> cost = costing_.movement_cost(movement)) row->second.cost_sold += *cost;
    }
    std::vector<BatchCostRow> result;
    for (auto& [id, row] : rows) result.push_back(std::move(row));
    std::sort(result.begin(), result.end(),
              [](const BatchCostRow& a, const BatchCostRow& b) { return a.batch.id > b.batch.id; });
    return result;
}

// --- Profit and loss --------------------------------------------------------------

ProfitLossColumn InsightsService::profit_loss_column(domain::Date from, domain::Date to) const {
    ProfitLossColumn column;
    for (const domain::Sale& sale : data_.sales) {
        if (!in(from, to, sale.at)) continue;
        for (const domain::SaleLine& line : sale.lines) {
            if (line.kind == domain::LineKind::Deposit || domain::is_expected(sale, line)) continue;
            column.revenue[sale.channel] += line.amount();
            column.total_revenue += line.amount();
        }
    }
    column.taproom_expected = expected_between(from, to);
    column.total_revenue += column.taproom_expected.amount;
    const CostingService::PeriodCosts costs = costing_.period_costs(domain::start_of(from), domain::end_of(to));
    column.cost_of_goods = costs.sold;
    column.beer_lost = costs.lost + costs.dumped;
    column.gross_profit = column.total_revenue - column.cost_of_goods;
    column.excise = ttb_.report({from, to}).tax.tax;
    for (const domain::ExpenseLine& line : expenses_.lines_between(from, to)) {
        column.expenses[line.category_id] += line.amount;
        column.total_expenses += line.amount;
    }
    column.net = column.gross_profit - column.excise - column.beer_lost - column.total_expenses;
    return column;
}

ProfitLoss InsightsService::profit_loss(const InsightsQuery& query) const {
    require_admin(session_, "see the profit and loss");
    ProfitLoss statement;
    statement.buckets = domain::buckets(query.period, query.grouping);
    statement.now = profit_loss_column(query.period.from, query.period.to);
    if (const std::optional<domain::Period> compared = domain::comparison_period(query.period, query.comparison)) {
        statement.before = profit_loss_column(compared->from, compared->to);
    }
    for (const domain::Bucket& bucket : statement.buckets) {
        const ProfitLossColumn column = profit_loss_column(bucket.first, bucket.last);
        statement.revenue_series.push_back(column.total_revenue);
        statement.cost_series.push_back(column.cost_of_goods + column.excise + column.beer_lost +
                                        column.total_expenses);
    }

    // By beer: what its sales brought in, and what the beer that left cost.
    std::map<std::string, ProfitLoss::BeerMargin> beers;
    const auto beer_margin = [&](const std::string& beer) -> ProfitLoss::BeerMargin& {
        ProfitLoss::BeerMargin& entry = beers[beer_key(beer)];
        if (entry.beer.empty()) entry.beer = beer;
        return entry;
    };
    for (const domain::Sale& sale : data_.sales) {
        if (!query.period.contains(domain::local_date(sale.at))) continue;
        for (const domain::SaleLine& line : sale.lines) {
            if (!line.unit_price) ++statement.unpriced_lines;
            if (line.kind != domain::LineKind::Deposit && !line.beer.empty()) {
                beer_margin(line.beer).revenue += counted(sale, line);
            }
        }
    }
    std::set<std::int64_t> estimated;
    for (const domain::PackagedMovement& movement : data_.ledgers.packaged) {
        if (!domain::counts_as_sold(movement.reason) || !query.period.contains(domain::local_date(movement.at))) {
            continue;
        }
        const domain::BatchFacts* facts = batch(movement.batch_id);
        if (!facts) continue;
        const std::optional<double> cost = costing_.movement_cost(movement);
        if (!cost) {
            ++statement.uncosted_removals;
            continue;
        }
        beer_margin(facts->beer).cost += *cost;
        const auto batch_cost = costing_.batches().find(movement.batch_id);
        if (batch_cost != costing_.batches().end() && batch_cost->second.basis >= domain::CostBasis::Estimated) {
            estimated.insert(movement.batch_id);
        }
    }
    statement.estimated_batches = static_cast<int>(estimated.size());
    for (auto& [key, entry] : beers) statement.beers.push_back(std::move(entry));
    std::sort(statement.beers.begin(), statement.beers.end(),
              [](const auto& a, const auto& b) { return a.revenue > b.revenue; });
    return statement;
}

} // namespace ninkasi::app
