#pragma once
// Batches of beer from brew day to packaging: brews, moving beer between
// vessels, readings, and keeping vessels clean.
//
// Brews are logged after the fact, the way brewers keep records: a brew's
// start and its knock-out can be logged together, or the knock-out a day
// later. A brew waiting for its knock-out to be logged doesn't hold up the
// brewhouse.
//
// Every action runs in one transaction and is written to the activity log
// under the session's user. Every change in the volume of beer in a vessel is
// a row in the volume ledger (volume_events); what a vessel holds is the sum
// of its rows. Mistakes are corrected with new rows, never by editing old ones.
//
// The rules, each enforced by throwing std::invalid_argument with a message
// for the brewer:
// - A vessel holds one batch at a time; a batch may be split across vessels.
// - Beer only goes into fermenters and brite tanks that are in service, big
//   enough, and empty (or, for another brew, holding the same batch).
// - A knock-out can't be before its brew started.
//
// Messages may contain quantity tokens; show them with
// domain::render_quantities so they appear in the brewer's units.
//
// What the floor draws every frame is kept in memory: the active batches
// (with brews and readings), what each vessel holds and vessel conditions.
// It is reloaded after every change.

#include "app/floor_service.h"
#include "app/session.h"
#include "data/batch_repository.h"
#include "data/database.h"
#include "domain/activity.h"
#include "domain/batch.h"
#include "domain/timestamp.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ninkasi::app {

// What a vessel holds.
struct VesselContents {
    std::int64_t batch_id = 0;
    double volume_l = 0.0;
};

// Whether a vessel can take beer and, if not, why. A vessel that can may
// still come with a warning, such as "Needs cleaning".
struct Availability {
    bool allowed = true;
    std::string message;
};

struct StartBrew {
    std::int64_t brewhouse_id = 0;
    domain::Timestamp at{};
    // A new batch of `recipe` (whose id, if not 0, is the recipe it came
    // from) with this code...
    std::string code;
    domain::Recipe recipe;
    // ...or another brew for an active batch.
    std::optional<std::int64_t> add_to_batch;
};

// The end of a brew: the wort goes into a vessel and the brewer confirms
// the ingredients actually used.
struct KnockOut {
    std::int64_t brew_id = 0;  // ignored when logged with the brew's start
    std::int64_t vessel_id = 0;
    domain::Timestamp at{};
    double volume_l = 0.0;
    std::optional<double> og;
    std::vector<domain::IngredientUse> ingredients;
    std::string notes;
};

// Beer moved from one vessel to another.
struct Transfer {
    std::int64_t from_vessel_id = 0;
    std::int64_t to_vessel_id = 0;
    domain::Timestamp at{};
    double volume_l = 0.0;       // arriving in the target
    bool empties_source = true;  // anything left behind is recorded as a loss
    std::string note;
};

struct Packaging {
    std::int64_t vessel_id = 0;
    domain::PackageFormat format = domain::PackageFormat::Kegs;
    domain::Timestamp at{};
    double volume_l = 0.0;
    bool empties_vessel = true;  // anything left behind is recorded as a loss
    std::string note;
};

class BatchService {
public:
    BatchService(data::Database& db, Session session, const FloorService& floor);

    // --- In memory: cheap enough to call every frame --------------------------

    // Brewing, fermenting and conditioning batches.
    const std::vector<domain::Batch>& active_batches() const { return active_; }
    const domain::Batch* find_active(std::int64_t batch_id) const;
    std::optional<VesselContents> contents(std::int64_t vessel_id) const;
    // The vessels holding a batch, with how much each holds.
    std::vector<std::pair<std::int64_t, double>> locations(std::int64_t batch_id) const;
    // A batch's volume in all vessels.
    double volume(std::int64_t batch_id) const;
    // Brews whose knock-out hasn't been logged yet, oldest first: at one
    // brewhouse, or at any.
    std::vector<domain::Brew> pending_brews(std::optional<std::int64_t> brewhouse_id = std::nullopt) const;
    // A brew of an active batch, and its batch; nulls if there's none.
    std::pair<const domain::Batch*, const domain::Brew*> find_brew(std::int64_t brew_id) const;
    domain::VesselCondition condition(std::int64_t vessel_id) const;
    // Grows with every change, so windows know when to re-read the database.
    std::uint64_t revision() const { return revision_; }

    // --- From the database ----------------------------------------------------

    std::vector<data::BatchSummary> list();
    std::optional<domain::Batch> load(std::int64_t batch_id);
    std::vector<domain::IngredientUse> ingredients(std::int64_t brew_id);
    std::vector<domain::VolumeEvent> volume_history(std::int64_t batch_id);
    std::vector<data::BrewSummary> brewhouse_history(std::int64_t brewhouse_id, int limit = 100);
    std::vector<domain::Activity> batch_activity(std::int64_t batch_id, int limit = 500);
    std::vector<domain::Activity> element_activity(std::int64_t element_id, int limit = 500);

    // --- Brewing ----------------------------------------------------------------

    // The code after the most recent batch's.
    std::string suggest_code();
    // Logs a brew, with its knock-out if given, in one go: if the knock-out
    // is refused nothing is saved. Returns the brew's id.
    std::int64_t log_brew(const StartBrew& start, const std::optional<KnockOut>& knock_out);
    // Logs just the start of a brew; its knock-out is logged later.
    std::int64_t start_brew(const StartBrew& request) { return log_brew(request, std::nullopt); }
    // Deletes a brew whose knock-out hasn't been logged. If it was a new
    // batch's only brew the batch goes too, freeing its code.
    void delete_brew(std::int64_t brew_id);
    // Whether a vessel can take `volume_l` of a batch: it must be empty or,
    // with `same_batch`, hold only that batch.
    Availability can_fill(std::int64_t vessel_id, double volume_l, std::optional<std::int64_t> same_batch) const;
    Availability can_knock_out(std::int64_t brew_id, std::int64_t vessel_id, double volume_l) const;
    void knock_out(const KnockOut& request);

    // --- Cellar -----------------------------------------------------------------

    Availability can_transfer(std::int64_t from_vessel_id, std::int64_t to_vessel_id, double volume_l) const;
    // Moving beer into a brite tank starts conditioning.
    void transfer(const Transfer& request);
    void package(const Packaging& request);
    // Dumps everything in the vessel.
    void dump(std::int64_t vessel_id, domain::Timestamp at, std::string_view reason);
    // Replaces the recorded volume with a measured one.
    void correct_volume(std::int64_t vessel_id, double measured_volume_l, domain::Timestamp at,
                        std::string_view note);
    // Between fermenting and conditioning; batches finish by leaving the vessels.
    void set_status(std::int64_t batch_id, domain::BatchStatus status, domain::Timestamp at);
    // Only for empty fermenters and brite tanks.
    void set_condition(std::int64_t vessel_id, domain::VesselCondition new_condition, domain::Timestamp at);

    // Needs a gravity, temperature or pH. Returns the reading's id.
    std::int64_t log_reading(const domain::Reading& reading);
    void delete_reading(std::int64_t reading_id);
    // A note on a batch's and/or an element's log.
    void add_note(std::optional<std::int64_t> batch_id, std::optional<std::int64_t> element_id, std::string_view text,
                  domain::Timestamp at);

private:
    // Throws if the knock-out can't be logged for a batch (nullopt: a new one).
    void check_knock_out(const KnockOut& request, std::optional<std::int64_t> batch_id,
                         domain::Timestamp started_at) const;
    // The vessel's element and contents; throws if it holds nothing.
    std::pair<const domain::Element*, VesselContents> occupied_vessel(std::int64_t vessel_id) const;
    const domain::Batch& active_batch(std::int64_t batch_id) const;
    std::string element_name(std::optional<std::int64_t> element_id) const;

    // Writers for use inside a transaction.
    void write_knock_out(const domain::Batch& batch, domain::Brew brew, const KnockOut& request);
    void record(const domain::VolumeEvent& event);
    void log(std::string_view kind, std::optional<std::int64_t> batch_id, std::optional<std::int64_t> element_id,
             std::optional<std::int64_t> other_element_id, const std::string& text, domain::Timestamp at);
    // Records what's left in a vessel after beer leaves it as a loss (or a
    // gain, if more left than was recorded) and marks the vessel dirty.
    // Returns the volume written off.
    double empty_vessel(const domain::Batch& batch, std::int64_t vessel_id, double remaining_l, domain::Timestamp at);
    // Finishes the batch if none of it is left in vessels.
    void finish_if_empty(const domain::Batch& batch, domain::Timestamp at);
    void reload();

    data::Database& db_;
    Session session_;
    const FloorService& floor_;
    data::BatchRepository repository_;

    std::vector<domain::Batch> active_;
    std::unordered_map<std::int64_t, VesselContents> contents_;
    std::unordered_map<std::int64_t, domain::VesselCondition> conditions_;
    std::uint64_t revision_ = 0;
};

} // namespace ninkasi::app
