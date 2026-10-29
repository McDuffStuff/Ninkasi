#include "data/analytics_repository.h"

#include "data/inventory_repository.h"
#include "data/sales_repository.h"

namespace ninkasi::data {

namespace {

constexpr int kAllSales = 1'000'000;

domain::Timestamp column_time(const Statement& stmt, int col) {
    return domain::parse_iso8601(stmt.column_text(col)).value_or(domain::Timestamp{});
}

} // namespace

AnalyticsData AnalyticsRepository::load() {
    AnalyticsData data;
    data.ledgers = CostingRepository(db_).load();
    data.sales = SalesRepository(db_).sales(kAllSales);
    data.items = InventoryRepository(db_).load_items(true);
    for (const auto& [customer, accounts] : SalesRepository(db_).keg_accounts()) {
        for (const domain::KegAccount& account : accounts) data.kegs_out += account.out();
    }

    auto brews = db_.prepare("SELECT batch_id, started_at, knocked_out_at, volume_l FROM brews;");
    while (brews.step()) {
        BrewFact brew;
        brew.batch_id = brews.column_int64(0);
        brew.started_at = column_time(brews, 1);
        if (!brews.column_is_null(2)) brew.knocked_out_at = column_time(brews, 2);
        brew.volume_l = brews.column_double(3);
        data.brews.push_back(brew);
    }

    auto events = db_.prepare("SELECT id, at, batch_id, vessel_id, volume_l, reason FROM volume_events;");
    while (events.step()) {
        domain::VolumeEvent event;
        event.id = events.column_int64(0);
        event.at = column_time(events, 1);
        event.batch_id = events.column_int64(2);
        event.vessel_id = events.column_optional_int64(3);
        event.volume_l = events.column_double(4);
        event.reason = domain::volume_reason_from_key(events.column_text(5)).value_or(domain::VolumeReason::Adjusted);
        data.volume_events.push_back(event);
    }

    // Vessels are the elements with a capacity.
    auto vessels = db_.prepare("SELECT id, name FROM elements WHERE capacity_l IS NOT NULL ORDER BY name;");
    while (vessels.step()) data.vessels.push_back({vessels.column_int64(0), vessels.column_text(1)});
    return data;
}

} // namespace ninkasi::data
