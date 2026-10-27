#pragma once
// Reads and writes batches, brews, readings, the volume ledger and vessel
// conditions. BatchService decides what may be written; this only stores it.

#include "data/database.h"
#include "domain/batch.h"
#include "domain/timestamp.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ninkasi::data {

// What a batch list shows, without loading every batch.
struct BatchSummary {
    std::int64_t id = 0;
    std::string code;
    std::string name;
    std::string style;
    domain::BatchStatus status = domain::BatchStatus::Brewing;
    domain::Timestamp created_at{};
    std::optional<domain::Timestamp> brewed_at;
    std::optional<domain::Timestamp> finished_at;
    std::optional<double> og;              // measured
    std::optional<double> latest_gravity;
};

// A brew and the batch it belongs to, for a brewhouse's history.
struct BrewSummary {
    domain::Brew brew;
    std::string code;
    std::string name;
};

// The volume of one batch in one vessel, from the ledger.
struct ContentsRow {
    std::int64_t vessel_id = 0;
    std::int64_t batch_id = 0;
    double volume_l = 0.0;
};

class BatchRepository {
public:
    explicit BatchRepository(Database& db) : db_(db) {}

    // --- Batches ------------------------------------------------------------
    // Inserts the batch (its id, brews and readings are ignored); returns the new id.
    std::int64_t insert_batch(const domain::Batch& batch);
    void set_status(std::int64_t batch_id, domain::BatchStatus status, std::optional<domain::Timestamp> finished_at);
    // Only for batches with no ledger rows (a first brew that was cancelled).
    void remove_batch(std::int64_t batch_id);
    // With brews and readings.
    std::optional<domain::Batch> load_batch(std::int64_t batch_id);
    // Every batch that is brewing, fermenting or conditioning, with brews and readings.
    std::vector<domain::Batch> load_active();
    // Every batch, newest first.
    std::vector<BatchSummary> list();
    // The code of the most recently created batch, if any.
    std::optional<std::string> latest_code();
    bool code_exists(std::string_view code);

    // --- Brews --------------------------------------------------------------
    std::int64_t insert_brew(const domain::Brew& brew, std::int64_t user_id);
    // Saves the knock-out: vessel, time, volume, OG and notes.
    void update_brew(const domain::Brew& brew);
    void remove_brew(std::int64_t brew_id);
    void insert_ingredients(std::int64_t brew_id, const std::vector<domain::IngredientUse>& ingredients);
    std::vector<domain::IngredientUse> ingredients(std::int64_t brew_id);
    // The brews made on a brewhouse, newest first.
    std::vector<BrewSummary> brewhouse_history(std::int64_t brewhouse_id, int limit);

    // --- Readings -----------------------------------------------------------
    std::int64_t insert_reading(const domain::Reading& reading, std::int64_t user_id);
    std::optional<domain::Reading> load_reading(std::int64_t reading_id);
    void remove_reading(std::int64_t reading_id);

    // --- Volume ledger ------------------------------------------------------
    void insert_volume_event(const domain::VolumeEvent& event, std::int64_t user_id);
    // Volumes of active batches by vessel. Rows that sum to (nearly) zero are left out.
    std::vector<ContentsRow> active_contents();
    // Every ledger row of a batch, oldest first.
    std::vector<domain::VolumeEvent> volume_events(std::int64_t batch_id);
    // The batch's volume in all vessels.
    double total_volume(std::int64_t batch_id);
    // Whether any of the batch was kegged, canned or bottled.
    bool was_packaged(std::int64_t batch_id);
    // Whether an element holds beer or has a brew under way: equipment in
    // use can't be removed from the floor.
    bool in_use(std::int64_t element_id);

    // --- Vessel conditions --------------------------------------------------
    // Vessels that aren't clean. Vessels missing from the map are clean.
    std::unordered_map<std::int64_t, domain::VesselCondition> conditions();
    void set_condition(std::int64_t vessel_id, domain::VesselCondition condition);

private:
    std::vector<domain::Batch> load_batches(std::string_view where, std::optional<std::int64_t> id);
    Database& db_;
};

} // namespace ninkasi::data
