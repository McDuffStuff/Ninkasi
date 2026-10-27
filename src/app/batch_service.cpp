#include "app/batch_service.h"

#include "data/activity_log.h"
#include "data/audit_log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace ninkasi::app {

namespace {

using domain::kEmptyVolumeL;
using domain::VolumeReason;

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

[[noreturn]] void fail(const std::string& message) {
    throw std::invalid_argument(message);
}

// Activity text with the brewer's note, if any, after it.
std::string with_note(std::string text, std::string_view note) {
    const std::string_view trimmed = trim(note);
    if (!trimmed.empty()) {
        text += ": ";
        text += trimmed;
    }
    return text;
}

// "1.012 · 18 °C · pH 4.40", with quantity tokens.
std::string describe(const domain::Reading& reading) {
    std::string text;
    const auto add = [&text](const std::string& part) {
        if (!text.empty()) text += " · ";
        text += part;
    };
    if (reading.gravity_sg) add(domain::gravity_token(*reading.gravity_sg));
    if (reading.temperature_c) add(domain::temperature_token(*reading.temperature_c));
    if (reading.ph) {
        char ph[32];
        std::snprintf(ph, sizeof ph, "pH %.2f", *reading.ph);
        add(ph);
    }
    return text;
}

} // namespace

BatchService::BatchService(data::Database& db, Session session, const FloorService& floor)
    : db_(db), session_(std::move(session)), floor_(floor), repository_(db) {
    reload();
}

// --- In memory --------------------------------------------------------------

const domain::Batch* BatchService::find_active(std::int64_t batch_id) const {
    for (const domain::Batch& batch : active_) {
        if (batch.id == batch_id) return &batch;
    }
    return nullptr;
}

std::optional<VesselContents> BatchService::contents(std::int64_t vessel_id) const {
    const auto found = contents_.find(vessel_id);
    if (found == contents_.end()) return std::nullopt;
    return found->second;
}

std::vector<std::pair<std::int64_t, double>> BatchService::locations(std::int64_t batch_id) const {
    std::vector<std::pair<std::int64_t, double>> vessels;
    for (const auto& [vessel_id, held] : contents_) {
        if (held.batch_id == batch_id) vessels.emplace_back(vessel_id, held.volume_l);
    }
    std::sort(vessels.begin(), vessels.end());
    return vessels;
}

double BatchService::volume(std::int64_t batch_id) const {
    double total = 0.0;
    for (const auto& [vessel_id, held] : contents_) {
        if (held.batch_id == batch_id) total += held.volume_l;
    }
    return total;
}

const domain::Brew* BatchService::brew_at(std::int64_t brewhouse_id) const {
    for (const domain::Batch& batch : active_) {
        for (const domain::Brew& brew : batch.brews) {
            if (brew.in_progress() && brew.brewhouse_id == brewhouse_id) return &brew;
        }
    }
    return nullptr;
}

domain::VesselCondition BatchService::condition(std::int64_t vessel_id) const {
    const auto found = conditions_.find(vessel_id);
    return found == conditions_.end() ? domain::VesselCondition::Clean : found->second;
}

// --- From the database --------------------------------------------------------

std::vector<data::BatchSummary> BatchService::list() {
    return repository_.list();
}

std::optional<domain::Batch> BatchService::load(std::int64_t batch_id) {
    return repository_.load_batch(batch_id);
}

std::vector<domain::IngredientUse> BatchService::ingredients(std::int64_t brew_id) {
    return repository_.ingredients(brew_id);
}

std::vector<domain::VolumeEvent> BatchService::volume_history(std::int64_t batch_id) {
    return repository_.volume_events(batch_id);
}

std::vector<data::BrewSummary> BatchService::brewhouse_history(std::int64_t brewhouse_id, int limit) {
    return repository_.brewhouse_history(brewhouse_id, limit);
}

std::vector<domain::Activity> BatchService::batch_activity(std::int64_t batch_id, int limit) {
    return data::activity_for_batch(db_, batch_id, limit);
}

std::vector<domain::Activity> BatchService::element_activity(std::int64_t element_id, int limit) {
    return data::activity_for_element(db_, element_id, limit);
}

// --- Brewing ----------------------------------------------------------------

std::string BatchService::suggest_code() {
    std::string code = domain::next_batch_code(repository_.latest_code().value_or(""));
    while (repository_.code_exists(code)) code = domain::next_batch_code(code);
    return code;
}

std::int64_t BatchService::start_brew(const StartBrew& request) {
    const domain::Element* brewhouse = floor_.find(request.brewhouse_id);
    if (!brewhouse || brewhouse->type != domain::ElementType::Brewhouse) fail("Brews are made on a brewhouse.");
    if (brew_at(request.brewhouse_id)) fail(brewhouse->name + " already has a brew under way.");

    domain::Brew brew;
    brew.brewhouse_id = request.brewhouse_id;
    brew.started_at = request.at;
    std::string text;

    data::Transaction tx(db_);
    if (request.add_to_batch) {
        const domain::Batch& batch = active_batch(*request.add_to_batch);
        brew.batch_id = batch.id;
        text = "Started brew " + std::to_string(batch.brews.size() + 1) + " of " + domain::label(batch) + " on " +
               brewhouse->name;
    } else {
        const std::string code(trim(request.code));
        if (code.empty()) fail("Give the batch a code.");
        if (repository_.code_exists(code)) fail("There is already a batch " + domain::batch_label(code, "") + ".");
        if (trim(request.recipe.name).empty()) fail("Choose a recipe to brew.");

        domain::Batch batch;
        batch.code = code;
        batch.recipe = request.recipe;
        batch.recipe.id = 0;
        if (request.recipe.id != 0) batch.recipe_id = request.recipe.id;
        batch.status = domain::BatchStatus::Brewing;
        batch.created_at = request.at;
        batch.id = repository_.insert_batch(batch);
        brew.batch_id = batch.id;
        text = "Started brewing " + domain::label(batch) + " on " + brewhouse->name;
    }
    const std::int64_t brew_id = repository_.insert_brew(brew, session_.user_id);
    log("brew", brew.batch_id, request.brewhouse_id, std::nullopt, text, request.at);
    tx.commit();
    reload();
    return brew_id;
}

void BatchService::cancel_brew(std::int64_t brew_id) {
    const auto [batch, brew] = find_brew(brew_id);
    if (!brew || !brew->in_progress()) fail("That brew isn't under way.");
    const std::string what = domain::label(*batch) + " on " + element_name(brew->brewhouse_id);

    data::Transaction tx(db_);
    if (batch->status == domain::BatchStatus::Brewing && batch->brews.size() == 1) {
        // Nothing was made: forget the batch, keeping only an audit record.
        repository_.remove_batch(batch->id);
        data::record_audit(db_, session_.user_id, "batch.cancel", "batch", batch->id,
                           "cancelled " + what + " before knock-out");
    } else {
        repository_.remove_brew(brew_id);
        log("brew", batch->id, brew->brewhouse_id, std::nullopt, "Cancelled a brew of " + what, domain::now());
    }
    tx.commit();
    reload();
}

Availability BatchService::can_knock_out(std::int64_t brew_id, std::int64_t vessel_id, double volume_l) const {
    const auto [batch, brew] = find_brew(brew_id);
    if (!brew || !brew->in_progress()) return {false, "That brew isn't under way."};
    return can_fill(vessel_id, volume_l, batch->id);
}

void BatchService::knock_out(const KnockOut& request) {
    const auto [batch, current] = find_brew(request.brew_id);
    if (!current || !current->in_progress()) fail("That brew isn't under way.");
    if (!(request.volume_l > 0.0)) fail("Enter the volume knocked out.");
    if (request.og && !(*request.og > 1.0 && *request.og < 1.2)) fail("That original gravity doesn't look right.");
    for (const domain::IngredientUse& use : request.ingredients) {
        if (!(use.actual >= 0.0)) fail("Ingredient amounts can't be negative.");
    }
    const Availability availability = can_fill(request.vessel_id, request.volume_l, batch->id);
    if (!availability.allowed) fail(element_name(request.vessel_id) + ": " + availability.message);

    domain::Brew brew = *current;
    brew.vessel_id = request.vessel_id;
    brew.knocked_out_at = request.at;
    brew.volume_l = request.volume_l;
    brew.og = request.og;
    brew.notes = trim(request.notes);

    std::string text = "Knocked out " + domain::volume_token(request.volume_l) + " of " + domain::label(*batch) +
                       " into " + element_name(request.vessel_id);
    if (request.og) text += " at " + domain::gravity_token(*request.og);

    data::Transaction tx(db_);
    repository_.update_brew(brew);
    repository_.insert_ingredients(brew.id, request.ingredients);
    record({0, request.at, batch->id, request.vessel_id, request.volume_l, VolumeReason::Brewed});
    // Full now; it needs cleaning again once it's emptied.
    repository_.set_condition(request.vessel_id, domain::VesselCondition::Clean);
    if (batch->status == domain::BatchStatus::Brewing) {
        repository_.set_status(batch->id, domain::BatchStatus::Fermenting, std::nullopt);
    }
    log("knock_out", batch->id, request.vessel_id, brew.brewhouse_id, with_note(text, brew.notes), request.at);
    tx.commit();
    reload();
}

// --- Cellar -----------------------------------------------------------------

Availability BatchService::can_transfer(std::int64_t from_vessel_id, std::int64_t to_vessel_id,
                                        double volume_l) const {
    if (!contents(from_vessel_id)) return {false, element_name(from_vessel_id) + " is empty."};
    if (from_vessel_id == to_vessel_id) return {false, "The beer is already here."};
    return can_fill(to_vessel_id, volume_l, std::nullopt);
}

void BatchService::transfer(const Transfer& request) {
    const auto [source, held] = occupied_vessel(request.from_vessel_id);
    if (!(request.volume_l > 0.0)) fail("Enter the volume transferred.");
    const Availability availability = can_transfer(request.from_vessel_id, request.to_vessel_id, request.volume_l);
    if (!availability.allowed) fail(element_name(request.to_vessel_id) + ": " + availability.message);
    if (!request.empties_source && request.volume_l >= held.volume_l - kEmptyVolumeL) {
        fail("That's everything in " + source->name + ". To leave some behind, transfer less.");
    }
    const domain::Batch& batch = active_batch(held.batch_id);
    const domain::Element& target = *floor_.find(request.to_vessel_id);

    std::string text = "Moved " + domain::volume_token(request.volume_l) + " of " + domain::label(batch) + " from " +
                       source->name + " to " + target.name;

    data::Transaction tx(db_);
    record({0, request.at, batch.id, request.from_vessel_id, -request.volume_l, VolumeReason::TransferOut});
    record({0, request.at, batch.id, request.to_vessel_id, request.volume_l, VolumeReason::TransferIn});
    repository_.set_condition(request.to_vessel_id, domain::VesselCondition::Clean);
    if (request.empties_source) {
        const double left_l = empty_vessel(batch, request.from_vessel_id, held.volume_l - request.volume_l, request.at);
        if (left_l > kEmptyVolumeL) text += ", leaving " + domain::volume_token(left_l) + " behind";
    } else {
        text += "; " + domain::volume_token(held.volume_l - request.volume_l) + " stays in " + source->name;
    }
    log("transfer", batch.id, request.from_vessel_id, request.to_vessel_id, with_note(text, request.note), request.at);

    if (target.type == domain::ElementType::BriteTank && batch.status == domain::BatchStatus::Fermenting) {
        repository_.set_status(batch.id, domain::BatchStatus::Conditioning, std::nullopt);
        log("status", batch.id, request.to_vessel_id, std::nullopt, domain::label(batch) + " is now conditioning",
            request.at);
    }
    tx.commit();
    reload();
}

void BatchService::package(const Packaging& request) {
    const auto [vessel, held] = occupied_vessel(request.vessel_id);
    if (!(request.volume_l > 0.0)) fail("Enter the volume packaged.");
    if (!request.empties_vessel && request.volume_l >= held.volume_l - kEmptyVolumeL) {
        fail("That's everything in " + vessel->name + ". To leave some behind, package less.");
    }
    const domain::Batch& batch = active_batch(held.batch_id);
    const bool kegs = request.format == domain::PackageFormat::Kegs;

    std::string text = std::string(kegs ? "Kegged " : "Canned/bottled ") + domain::volume_token(request.volume_l) +
                       " of " + domain::label(batch) + " from " + vessel->name;

    data::Transaction tx(db_);
    record({0, request.at, batch.id, request.vessel_id, -request.volume_l,
            kegs ? VolumeReason::Kegged : VolumeReason::Bottled});
    if (request.empties_vessel) {
        const double left_l = empty_vessel(batch, request.vessel_id, held.volume_l - request.volume_l, request.at);
        if (left_l > kEmptyVolumeL) text += ", leaving " + domain::volume_token(left_l) + " behind";
    } else {
        text += "; " + domain::volume_token(held.volume_l - request.volume_l) + " stays in " + vessel->name;
    }
    log("package", batch.id, request.vessel_id, std::nullopt, with_note(text, request.note), request.at);
    finish_if_empty(batch, request.at);
    tx.commit();
    reload();
}

void BatchService::dump(std::int64_t vessel_id, domain::Timestamp at, std::string_view reason) {
    const auto [vessel, held] = occupied_vessel(vessel_id);
    const domain::Batch& batch = active_batch(held.batch_id);
    const std::string text =
        "Dumped " + domain::volume_token(held.volume_l) + " of " + domain::label(batch) + " from " + vessel->name;

    data::Transaction tx(db_);
    record({0, at, batch.id, vessel_id, -held.volume_l, VolumeReason::Dumped});
    repository_.set_condition(vessel_id, domain::VesselCondition::Dirty);
    log("dump", batch.id, vessel_id, std::nullopt, with_note(text, reason), at);
    finish_if_empty(batch, at);
    tx.commit();
    reload();
}

void BatchService::correct_volume(std::int64_t vessel_id, double measured_volume_l, domain::Timestamp at,
                                  std::string_view note) {
    const auto [vessel, held] = occupied_vessel(vessel_id);
    if (!(measured_volume_l >= 0.0)) fail("A volume can't be negative.");
    const double change_l = measured_volume_l - held.volume_l;
    if (std::abs(change_l) <= kEmptyVolumeL) return;
    const domain::Batch& batch = active_batch(held.batch_id);
    const std::string text = "Corrected the volume of " + domain::label(batch) + " in " + vessel->name + " from " +
                             domain::volume_token(held.volume_l) + " to " + domain::volume_token(measured_volume_l);

    data::Transaction tx(db_);
    record({0, at, batch.id, vessel_id, change_l, VolumeReason::Adjusted});
    log("volume", batch.id, vessel_id, std::nullopt, with_note(text, note), at);
    if (measured_volume_l <= kEmptyVolumeL) {
        repository_.set_condition(vessel_id, domain::VesselCondition::Dirty);
        finish_if_empty(batch, at);
    }
    tx.commit();
    reload();
}

void BatchService::set_status(std::int64_t batch_id, domain::BatchStatus status, domain::Timestamp at) {
    if (status != domain::BatchStatus::Fermenting && status != domain::BatchStatus::Conditioning) {
        fail("A batch can only be switched between fermenting and conditioning.");
    }
    const domain::Batch& batch = active_batch(batch_id);
    if (batch.status == domain::BatchStatus::Brewing) fail(domain::label(batch) + " hasn't been knocked out yet.");
    if (batch.status == status) return;

    // Shown on the log of the vessel holding it (the first, if it's split).
    const std::vector<std::pair<std::int64_t, double>> vessels = locations(batch_id);
    const std::optional<std::int64_t> vessel_id =
        vessels.empty() ? std::nullopt : std::optional<std::int64_t>(vessels.front().first);
    const char* now_doing = status == domain::BatchStatus::Fermenting ? " is fermenting again" : " is now conditioning";

    data::Transaction tx(db_);
    repository_.set_status(batch_id, status, std::nullopt);
    log("status", batch_id, vessel_id, std::nullopt, domain::label(batch) + now_doing, at);
    tx.commit();
    reload();
}

void BatchService::set_condition(std::int64_t vessel_id, domain::VesselCondition new_condition, domain::Timestamp at) {
    const domain::Element* vessel = floor_.find(vessel_id);
    if (!vessel || !domain::holds_beer(vessel->type)) fail("Only fermenters and brite tanks are tracked for cleaning.");
    if (contents(vessel_id)) fail(vessel->name + " isn't empty.");
    const domain::VesselCondition previous = condition(vessel_id);
    if (previous == new_condition) return;

    std::string text;
    switch (new_condition) {
    case domain::VesselCondition::Clean:
        text = previous == domain::VesselCondition::OutOfService ? vessel->name + " is back in service"
                                                                 : "Cleaned " + vessel->name;
        break;
    case domain::VesselCondition::Dirty:        text = vessel->name + " needs cleaning"; break;
    case domain::VesselCondition::OutOfService: text = vessel->name + " is out of service"; break;
    }

    data::Transaction tx(db_);
    repository_.set_condition(vessel_id, new_condition);
    log("condition", std::nullopt, vessel_id, std::nullopt, text, at);
    tx.commit();
    reload();
}

std::int64_t BatchService::log_reading(const domain::Reading& reading) {
    if (!reading.gravity_sg && !reading.temperature_c && !reading.ph) fail("Enter a gravity, temperature or pH.");
    if (reading.gravity_sg && !(*reading.gravity_sg > 0.95 && *reading.gravity_sg < 1.2)) {
        fail("That gravity doesn't look right.");
    }
    if (reading.temperature_c && !(*reading.temperature_c > -20.0 && *reading.temperature_c < 110.0)) {
        fail("That temperature doesn't look right.");
    }
    if (reading.ph && !(*reading.ph > 0.0 && *reading.ph < 14.0)) fail("pH must be between 0 and 14.");

    // Readings can also be added to finished batches (a final gravity taken late).
    std::string batch_label;
    if (const domain::Batch* batch = find_active(reading.batch_id)) {
        batch_label = domain::label(*batch);
    } else if (const std::optional<domain::Batch> finished = repository_.load_batch(reading.batch_id)) {
        batch_label = domain::label(*finished);
    } else {
        fail("There is no such batch.");
    }

    data::Transaction tx(db_);
    const std::int64_t id = repository_.insert_reading(reading, session_.user_id);
    log("reading", reading.batch_id, reading.vessel_id, std::nullopt,
        with_note(batch_label + ": " + describe(reading), reading.note), reading.at);
    tx.commit();
    reload();
    return id;
}

void BatchService::delete_reading(std::int64_t reading_id) {
    const std::optional<domain::Reading> reading = repository_.load_reading(reading_id);
    if (!reading) return;

    data::Transaction tx(db_);
    repository_.remove_reading(reading_id);
    log("reading", reading->batch_id, reading->vessel_id, std::nullopt, "Deleted a reading: " + describe(*reading),
        domain::now());
    tx.commit();
    reload();
}

void BatchService::add_note(std::optional<std::int64_t> batch_id, std::optional<std::int64_t> element_id,
                            std::string_view text, domain::Timestamp at) {
    const std::string_view note = trim(text);
    if (note.empty()) fail("Write a note first.");
    if (!batch_id && !element_id) fail("A note needs a batch or a piece of equipment.");

    data::Transaction tx(db_);
    log("note", batch_id, element_id, std::nullopt, std::string(note), at);
    tx.commit();
    ++revision_;
}

// --- Helpers ----------------------------------------------------------------

Availability BatchService::can_fill(std::int64_t vessel_id, double volume_l,
                                    std::optional<std::int64_t> same_batch) const {
    const domain::Element* vessel = floor_.find(vessel_id);
    if (!vessel || !domain::holds_beer(vessel->type)) return {false, "Only fermenters and brite tanks hold beer."};
    const domain::VesselCondition vessel_condition = condition(vessel_id);
    if (vessel_condition == domain::VesselCondition::OutOfService) return {false, "Out of service."};

    double already_l = 0.0;
    if (const std::optional<VesselContents> held = contents(vessel_id)) {
        if (!same_batch || held->batch_id != *same_batch) {
            const domain::Batch* other = find_active(held->batch_id);
            return {false, other ? "Holds " + domain::label(*other) + "." : std::string("Not empty.")};
        }
        already_l = held->volume_l;
    }
    if (vessel->capacity_l && already_l + volume_l > *vessel->capacity_l + kEmptyVolumeL) {
        if (already_l > 0.0) {
            return {false, "Not enough room: " + domain::volume_token(*vessel->capacity_l - already_l) + " free."};
        }
        return {false, "Too small: holds " + domain::volume_token(*vessel->capacity_l) + "."};
    }
    if (vessel_condition == domain::VesselCondition::Dirty) return {true, "Needs cleaning."};
    if (!vessel->capacity_l) return {true, "Capacity not set, so it can't be checked."};
    return {true, {}};
}

std::pair<const domain::Element*, VesselContents> BatchService::occupied_vessel(std::int64_t vessel_id) const {
    const domain::Element* vessel = floor_.find(vessel_id);
    if (!vessel) fail("That equipment is no longer on the floor.");
    const std::optional<VesselContents> held = contents(vessel_id);
    if (!held) fail(vessel->name + " is empty.");
    return {vessel, *held};
}

const domain::Batch& BatchService::active_batch(std::int64_t batch_id) const {
    const domain::Batch* batch = find_active(batch_id);
    if (!batch) fail("That batch is finished.");
    return *batch;
}

std::pair<const domain::Batch*, const domain::Brew*> BatchService::find_brew(std::int64_t brew_id) const {
    for (const domain::Batch& batch : active_) {
        for (const domain::Brew& brew : batch.brews) {
            if (brew.id == brew_id) return {&batch, &brew};
        }
    }
    return {nullptr, nullptr};
}

std::string BatchService::element_name(std::optional<std::int64_t> element_id) const {
    const domain::Element* element = element_id ? floor_.find(*element_id) : nullptr;
    return element ? element->name : std::string("removed equipment");
}

void BatchService::record(const domain::VolumeEvent& event) {
    repository_.insert_volume_event(event, session_.user_id);
}

void BatchService::log(std::string_view kind, std::optional<std::int64_t> batch_id,
                       std::optional<std::int64_t> element_id, std::optional<std::int64_t> other_element_id,
                       const std::string& text, domain::Timestamp at) {
    domain::Activity entry;
    entry.at = at;
    entry.kind = kind;
    entry.batch_id = batch_id;
    entry.element_id = element_id;
    entry.other_element_id = other_element_id;
    entry.text = text;
    data::record_activity(db_, session_.user_id, entry);
}

double BatchService::empty_vessel(const domain::Batch& batch, std::int64_t vessel_id, double remaining_l,
                                  domain::Timestamp at) {
    if (remaining_l > kEmptyVolumeL) {
        record({0, at, batch.id, vessel_id, -remaining_l, VolumeReason::Loss});
    } else if (remaining_l < -kEmptyVolumeL) {
        // More came out than was recorded, so the recorded volume was low.
        record({0, at, batch.id, vessel_id, -remaining_l, VolumeReason::Adjusted});
    }
    repository_.set_condition(vessel_id, domain::VesselCondition::Dirty);
    return remaining_l;
}

void BatchService::finish_if_empty(const domain::Batch& batch, domain::Timestamp at) {
    if (batch.brew_in_progress() || repository_.total_volume(batch.id) > kEmptyVolumeL) return;
    const bool packaged = repository_.was_packaged(batch.id);
    repository_.set_status(batch.id, packaged ? domain::BatchStatus::Packaged : domain::BatchStatus::Dumped, at);
    log("finished", batch.id, std::nullopt, std::nullopt,
        domain::label(batch) + (packaged ? " is finished: packaged" : " is finished: dumped"), at);
}

void BatchService::reload() {
    active_ = repository_.load_active();
    contents_.clear();
    for (const data::ContentsRow& row : repository_.active_contents()) {
        contents_[row.vessel_id] = {row.batch_id, row.volume_l};
    }
    conditions_ = repository_.conditions();
    ++revision_;
}

} // namespace ninkasi::app
