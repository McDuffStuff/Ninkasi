#include "data/batch_repository.h"

#include "data/recipe_json.h"

namespace ninkasi::data {

namespace {

constexpr std::string_view kActiveCondition = "status IN ('brewing', 'fermenting', 'conditioning')";

domain::Timestamp column_time(const Statement& stmt, int col) {
    return domain::parse_iso8601(stmt.column_text(col)).value_or(domain::Timestamp{});
}

std::optional<domain::Timestamp> column_optional_time(const Statement& stmt, int col) {
    if (stmt.column_is_null(col)) return std::nullopt;
    return domain::parse_iso8601(stmt.column_text(col));
}

void bind_time(Statement& stmt, int index, std::optional<domain::Timestamp> time) {
    if (time) {
        stmt.bind(index, domain::to_iso8601(*time));
    } else {
        stmt.bind_null(index);
    }
}

constexpr std::string_view kBrewColumns =
    "id, batch_id, brewhouse_id, vessel_id, started_at, knocked_out_at, volume_l, og, notes";

domain::Brew read_brew(const Statement& stmt) {
    domain::Brew brew;
    brew.id = stmt.column_int64(0);
    brew.batch_id = stmt.column_int64(1);
    brew.brewhouse_id = stmt.column_optional_int64(2);
    brew.vessel_id = stmt.column_optional_int64(3);
    brew.started_at = column_time(stmt, 4);
    brew.knocked_out_at = column_optional_time(stmt, 5);
    brew.volume_l = stmt.column_double(6);
    brew.og = stmt.column_optional_double(7);
    brew.notes = stmt.column_text(8);
    return brew;
}

constexpr std::string_view kReadingColumns =
    "id, batch_id, vessel_id, at, method, gravity_sg, raw_value, temperature_c, ph, note";

domain::Reading read_reading(const Statement& stmt) {
    domain::Reading reading;
    reading.id = stmt.column_int64(0);
    reading.batch_id = stmt.column_int64(1);
    reading.vessel_id = stmt.column_optional_int64(2);
    reading.at = column_time(stmt, 3);
    reading.method = domain::reading_method_from_key(stmt.column_text(4)).value_or(domain::ReadingMethod::Digital);
    reading.gravity_sg = stmt.column_optional_double(5);
    reading.raw_value = stmt.column_optional_double(6);
    reading.temperature_c = stmt.column_optional_double(7);
    reading.ph = stmt.column_optional_double(8);
    reading.note = stmt.column_text(9);
    return reading;
}

} // namespace

// --- Batches ----------------------------------------------------------------

std::int64_t BatchRepository::insert_batch(const domain::Batch& batch) {
    auto stmt = db_.prepare(
        "INSERT INTO batches (code, name, style, recipe_id, recipe, status, created_at) VALUES (?, ?, ?, ?, ?, ?, ?);");
    stmt.bind(1, batch.code).bind(2, batch.recipe.name).bind(3, batch.recipe.style);
    stmt.bind_optional(4, batch.recipe_id);
    stmt.bind(5, to_json(batch.recipe)).bind(6, domain::key(batch.status)).bind(7, domain::to_iso8601(batch.created_at));
    stmt.execute();
    return db_.last_insert_id();
}

void BatchRepository::set_status(std::int64_t batch_id, domain::BatchStatus status,
                                 std::optional<domain::Timestamp> finished_at) {
    auto stmt = db_.prepare("UPDATE batches SET status = ?, finished_at = ? WHERE id = ?;");
    stmt.bind(1, domain::key(status));
    bind_time(stmt, 2, finished_at);
    stmt.bind(3, batch_id);
    stmt.execute();
}

void BatchRepository::remove_batch(std::int64_t batch_id) {
    auto stmt = db_.prepare("DELETE FROM batches WHERE id = ?;");
    stmt.bind(1, batch_id);
    stmt.execute();
}

std::optional<domain::Batch> BatchRepository::load_batch(std::int64_t batch_id) {
    std::vector<domain::Batch> batches = load_batches("id = ?", batch_id);
    if (batches.empty()) return std::nullopt;
    return std::move(batches.front());
}

std::vector<domain::Batch> BatchRepository::load_active() {
    return load_batches(kActiveCondition, std::nullopt);
}

std::vector<domain::Batch> BatchRepository::load_batches(std::string_view where, std::optional<std::int64_t> id) {
    // `where` selects rows of `batches`; it may use one parameter, bound to `id`.
    const std::string condition(where);
    std::vector<domain::Batch> batches;
    std::unordered_map<std::int64_t, std::size_t> index_by_id;

    auto stmt = db_.prepare("SELECT id, code, name, recipe_id, recipe, status, created_at, finished_at FROM batches WHERE " +
                            condition + " ORDER BY id;");
    if (id) stmt.bind(1, *id);
    while (stmt.step()) {
        const std::optional<domain::BatchStatus> status = domain::batch_status_from_key(stmt.column_text(5));
        if (!status) continue;

        domain::Batch batch;
        batch.id = stmt.column_int64(0);
        batch.code = stmt.column_text(1);
        try {
            batch.recipe = recipe_from_json(stmt.column_text(4));
        } catch (const DbError&) {
            // Keep the batch even if its copy of the recipe is unreadable.
        }
        batch.recipe.id = 0;
        if (batch.recipe.name.empty()) batch.recipe.name = stmt.column_text(2);
        batch.recipe_id = stmt.column_optional_int64(3);
        batch.status = *status;
        batch.created_at = column_time(stmt, 6);
        batch.finished_at = column_optional_time(stmt, 7);

        index_by_id[batch.id] = batches.size();
        batches.push_back(std::move(batch));
    }

    const std::string of_these_batches = " WHERE batch_id IN (SELECT id FROM batches WHERE " + condition + ")";
    auto brews = db_.prepare("SELECT " + std::string(kBrewColumns) + " FROM brews" + of_these_batches +
                             " ORDER BY started_at, id;");
    if (id) brews.bind(1, *id);
    while (brews.step()) {
        domain::Brew brew = read_brew(brews);
        if (const auto found = index_by_id.find(brew.batch_id); found != index_by_id.end()) {
            batches[found->second].brews.push_back(std::move(brew));
        }
    }

    auto readings = db_.prepare("SELECT " + std::string(kReadingColumns) + " FROM readings" + of_these_batches +
                                " ORDER BY at, id;");
    if (id) readings.bind(1, *id);
    while (readings.step()) {
        domain::Reading reading = read_reading(readings);
        if (const auto found = index_by_id.find(reading.batch_id); found != index_by_id.end()) {
            batches[found->second].readings.push_back(std::move(reading));
        }
    }
    return batches;
}

std::vector<BatchSummary> BatchRepository::list() {
    std::vector<BatchSummary> batches;
    auto stmt = db_.prepare(R"sql(
        SELECT b.id, b.code, b.name, b.style, b.status, b.created_at, b.finished_at,
               (SELECT MIN(knocked_out_at) FROM brews WHERE batch_id = b.id),
               (SELECT SUM(og * volume_l) / SUM(volume_l) FROM brews
                 WHERE batch_id = b.id AND og IS NOT NULL AND knocked_out_at IS NOT NULL AND volume_l > 0),
               (SELECT gravity_sg FROM readings WHERE batch_id = b.id AND gravity_sg IS NOT NULL
                 ORDER BY at DESC, id DESC LIMIT 1)
        FROM batches b
        ORDER BY b.created_at DESC, b.id DESC;
    )sql");
    while (stmt.step()) {
        const std::optional<domain::BatchStatus> status = domain::batch_status_from_key(stmt.column_text(4));
        if (!status) continue;
        BatchSummary batch;
        batch.id = stmt.column_int64(0);
        batch.code = stmt.column_text(1);
        batch.name = stmt.column_text(2);
        batch.style = stmt.column_text(3);
        batch.status = *status;
        batch.created_at = column_time(stmt, 5);
        batch.finished_at = column_optional_time(stmt, 6);
        batch.brewed_at = column_optional_time(stmt, 7);
        batch.og = stmt.column_optional_double(8);
        batch.latest_gravity = stmt.column_optional_double(9);
        batches.push_back(std::move(batch));
    }
    return batches;
}

std::optional<std::string> BatchRepository::latest_code() {
    auto stmt = db_.prepare("SELECT code FROM batches ORDER BY id DESC LIMIT 1;");
    if (!stmt.step()) return std::nullopt;
    return stmt.column_text(0);
}

bool BatchRepository::code_exists(std::string_view code) {
    auto stmt = db_.prepare("SELECT 1 FROM batches WHERE code = ?;");
    stmt.bind(1, code);
    return stmt.step();
}

// --- Brews ------------------------------------------------------------------

std::int64_t BatchRepository::insert_brew(const domain::Brew& brew, std::int64_t user_id) {
    auto stmt = db_.prepare(
        "INSERT INTO brews (batch_id, brewhouse_id, vessel_id, started_at, knocked_out_at, volume_l, og, notes, user_id) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);");
    stmt.bind(1, brew.batch_id);
    stmt.bind_optional(2, brew.brewhouse_id);
    stmt.bind_optional(3, brew.vessel_id);
    stmt.bind(4, domain::to_iso8601(brew.started_at));
    bind_time(stmt, 5, brew.knocked_out_at);
    stmt.bind(6, brew.volume_l);
    stmt.bind_optional(7, brew.og);
    stmt.bind(8, brew.notes).bind(9, user_id);
    stmt.execute();
    return db_.last_insert_id();
}

void BatchRepository::update_brew(const domain::Brew& brew) {
    auto stmt = db_.prepare(
        "UPDATE brews SET vessel_id = ?, knocked_out_at = ?, volume_l = ?, og = ?, notes = ? WHERE id = ?;");
    stmt.bind_optional(1, brew.vessel_id);
    bind_time(stmt, 2, brew.knocked_out_at);
    stmt.bind(3, brew.volume_l);
    stmt.bind_optional(4, brew.og);
    stmt.bind(5, brew.notes).bind(6, brew.id);
    stmt.execute();
}

void BatchRepository::remove_brew(std::int64_t brew_id) {
    auto stmt = db_.prepare("DELETE FROM brews WHERE id = ?;");
    stmt.bind(1, brew_id);
    stmt.execute();
}

void BatchRepository::insert_ingredients(std::int64_t brew_id, const std::vector<domain::IngredientUse>& ingredients) {
    auto stmt = db_.prepare(
        "INSERT INTO brew_ingredients (brew_id, position, kind, library_id, name, detail, amount_kind, planned, actual, "
        "text) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
    int position = 0;
    for (const domain::IngredientUse& use : ingredients) {
        stmt.reset();
        stmt.bind(1, brew_id).bind(2, position++).bind(3, domain::key(use.kind));
        stmt.bind_optional(4, use.library_id != 0 ? std::optional<std::int64_t>(use.library_id) : std::nullopt);
        stmt.bind(5, use.name).bind(6, use.detail).bind(7, domain::key(use.amount_kind));
        stmt.bind(8, use.planned).bind(9, use.actual).bind(10, use.text);
        stmt.execute();
    }
}

std::vector<domain::IngredientUse> BatchRepository::ingredients(std::int64_t brew_id) {
    std::vector<domain::IngredientUse> uses;
    auto stmt = db_.prepare(
        "SELECT kind, library_id, name, detail, amount_kind, planned, actual, text FROM brew_ingredients "
        "WHERE brew_id = ? ORDER BY position;");
    stmt.bind(1, brew_id);
    while (stmt.step()) {
        const std::optional<domain::IngredientKind> kind = domain::ingredient_kind_from_key(stmt.column_text(0));
        const std::optional<domain::AmountKind> amount_kind = domain::amount_kind_from_key(stmt.column_text(4));
        if (!kind || !amount_kind) continue;
        uses.push_back({*kind, stmt.column_optional_int64(1).value_or(0), stmt.column_text(2), stmt.column_text(3),
                        *amount_kind, stmt.column_double(5), stmt.column_double(6), stmt.column_text(7)});
    }
    return uses;
}

std::vector<BrewSummary> BatchRepository::brewhouse_history(std::int64_t brewhouse_id, int limit) {
    std::vector<BrewSummary> brews;
    // The brew columns in kBrewColumns order, then the batch's code and name.
    auto stmt = db_.prepare(
        "SELECT br.id, br.batch_id, br.brewhouse_id, br.vessel_id, br.started_at, br.knocked_out_at, br.volume_l, "
        "br.og, br.notes, b.code, b.name FROM brews br JOIN batches b ON b.id = br.batch_id "
        "WHERE br.brewhouse_id = ? ORDER BY br.started_at DESC, br.id DESC LIMIT ?;");
    stmt.bind(1, brewhouse_id).bind(2, limit);
    while (stmt.step()) brews.push_back({read_brew(stmt), stmt.column_text(9), stmt.column_text(10)});
    return brews;
}

// --- Readings ---------------------------------------------------------------

std::int64_t BatchRepository::insert_reading(const domain::Reading& reading, std::int64_t user_id) {
    auto stmt = db_.prepare(
        "INSERT INTO readings (batch_id, vessel_id, at, method, gravity_sg, raw_value, temperature_c, ph, note, user_id) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
    stmt.bind(1, reading.batch_id);
    stmt.bind_optional(2, reading.vessel_id);
    stmt.bind(3, domain::to_iso8601(reading.at)).bind(4, domain::key(reading.method));
    stmt.bind_optional(5, reading.gravity_sg);
    stmt.bind_optional(6, reading.raw_value);
    stmt.bind_optional(7, reading.temperature_c);
    stmt.bind_optional(8, reading.ph);
    stmt.bind(9, reading.note).bind(10, user_id);
    stmt.execute();
    return db_.last_insert_id();
}

std::optional<domain::Reading> BatchRepository::load_reading(std::int64_t reading_id) {
    auto stmt = db_.prepare("SELECT " + std::string(kReadingColumns) + " FROM readings WHERE id = ?;");
    stmt.bind(1, reading_id);
    if (!stmt.step()) return std::nullopt;
    return read_reading(stmt);
}

void BatchRepository::remove_reading(std::int64_t reading_id) {
    auto stmt = db_.prepare("DELETE FROM readings WHERE id = ?;");
    stmt.bind(1, reading_id);
    stmt.execute();
}

// --- Volume ledger ----------------------------------------------------------

void BatchRepository::insert_volume_event(const domain::VolumeEvent& event, std::int64_t user_id) {
    auto stmt = db_.prepare(
        "INSERT INTO volume_events (at, batch_id, vessel_id, volume_l, reason, user_id) VALUES (?, ?, ?, ?, ?, ?);");
    stmt.bind(1, domain::to_iso8601(event.at)).bind(2, event.batch_id);
    stmt.bind_optional(3, event.vessel_id);
    stmt.bind(4, event.volume_l).bind(5, domain::key(event.reason)).bind(6, user_id);
    stmt.execute();
}

std::vector<ContentsRow> BatchRepository::active_contents() {
    std::vector<ContentsRow> rows;
    auto stmt = db_.prepare("SELECT vessel_id, batch_id, SUM(volume_l) FROM volume_events "
                            "WHERE vessel_id IS NOT NULL AND batch_id IN (SELECT id FROM batches WHERE " +
                            std::string(kActiveCondition) + ") GROUP BY vessel_id, batch_id ORDER BY vessel_id, batch_id;");
    while (stmt.step()) {
        const double volume_l = stmt.column_double(2);
        if (volume_l > domain::kEmptyVolumeL) rows.push_back({stmt.column_int64(0), stmt.column_int64(1), volume_l});
    }
    return rows;
}

std::vector<domain::VolumeEvent> BatchRepository::volume_events(std::int64_t batch_id) {
    std::vector<domain::VolumeEvent> events;
    auto stmt = db_.prepare(
        "SELECT id, at, batch_id, vessel_id, volume_l, reason FROM volume_events WHERE batch_id = ? ORDER BY at, id;");
    stmt.bind(1, batch_id);
    while (stmt.step()) {
        events.push_back({stmt.column_int64(0), column_time(stmt, 1), stmt.column_int64(2),
                          stmt.column_optional_int64(3), stmt.column_double(4),
                          domain::volume_reason_from_key(stmt.column_text(5)).value_or(domain::VolumeReason::Adjusted)});
    }
    return events;
}

double BatchRepository::total_volume(std::int64_t batch_id) {
    auto stmt = db_.prepare("SELECT COALESCE(SUM(volume_l), 0) FROM volume_events WHERE batch_id = ?;");
    stmt.bind(1, batch_id);
    stmt.step();
    return stmt.column_double(0);
}

bool BatchRepository::in_use(std::int64_t element_id) {
    auto stmt = db_.prepare(
        "SELECT 1 FROM brews WHERE brewhouse_id = ?1 AND knocked_out_at IS NULL "
        "UNION ALL "
        "SELECT 1 FROM volume_events WHERE vessel_id = ?1 GROUP BY batch_id HAVING SUM(volume_l) > ?2 "
        "LIMIT 1;");
    stmt.bind(1, element_id).bind(2, domain::kEmptyVolumeL);
    return stmt.step();
}

bool BatchRepository::was_packaged(std::int64_t batch_id) {
    auto stmt = db_.prepare(
        "SELECT 1 FROM volume_events WHERE batch_id = ? AND reason IN ('kegged', 'bottled') LIMIT 1;");
    stmt.bind(1, batch_id);
    return stmt.step();
}

// --- Vessel conditions ------------------------------------------------------

std::unordered_map<std::int64_t, domain::VesselCondition> BatchRepository::conditions() {
    std::unordered_map<std::int64_t, domain::VesselCondition> conditions;
    auto stmt = db_.prepare("SELECT vessel_id, condition FROM vessel_conditions;");
    while (stmt.step()) {
        const std::optional<domain::VesselCondition> condition =
            domain::vessel_condition_from_key(stmt.column_text(1));
        if (condition && *condition != domain::VesselCondition::Clean) conditions[stmt.column_int64(0)] = *condition;
    }
    return conditions;
}

void BatchRepository::set_condition(std::int64_t vessel_id, domain::VesselCondition condition) {
    if (condition == domain::VesselCondition::Clean) {
        auto stmt = db_.prepare("DELETE FROM vessel_conditions WHERE vessel_id = ?;");
        stmt.bind(1, vessel_id);
        stmt.execute();
        return;
    }
    auto stmt = db_.prepare(
        "INSERT INTO vessel_conditions (vessel_id, condition) VALUES (?, ?) ON CONFLICT(vessel_id) DO UPDATE SET "
        "condition = excluded.condition, changed_at = strftime('%Y-%m-%dT%H:%M:%SZ', 'now');");
    stmt.bind(1, vessel_id).bind(2, domain::key(condition));
    stmt.execute();
}

} // namespace ninkasi::data
