#include "app/packaged_goods_service.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace ninkasi::app {

namespace {

[[noreturn]] void fail(const std::string& message) {
    throw std::invalid_argument(message);
}

std::size_t index_of(domain::PackageType type) {
    return static_cast<std::size_t>(type);
}

bool same_name(std::string_view a, std::string_view b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

// "There are only 6 dirty sixtels.", for a request for more kegs than there are.
std::string only_text(int count, domain::KegState state, domain::PackageType type) {
    const std::string kegs = domain::count_text(type, count);  // "6 sixtels"
    const std::size_t space = kegs.find(' ');
    const std::string noun = (state == domain::KegState::Dirty ? "dirty " : "clean ") + kegs.substr(space + 1);
    if (count == 0) return "There are no " + noun + ".";
    return (count == 1 ? "There is only " : "There are only ") + kegs.substr(0, space + 1) + noun + ".";
}

} // namespace

PackagedGoodsService::PackagedGoodsService(data::Database& db, Session session)
    : db_(db), session_(std::move(session)), repository_(db) {
    refresh();
}

domain::KegCounts PackagedGoodsService::kegs(domain::PackageType type) const {
    return kegs_[index_of(type)];
}

std::vector<data::KegMovementRow> PackagedGoodsService::keg_history(int limit) {
    return repository_.keg_movements(limit);
}

std::vector<data::PackagedMovementRow> PackagedGoodsService::packaged_history(int limit) {
    return repository_.packaged_movements(limit);
}

std::vector<domain::PackagedStock> PackagedGoodsService::packaged_ever(int limit) {
    return repository_.packaged_ever(limit);
}

// --- Packaged beer ----------------------------------------------------------------

std::optional<domain::PackagedProduct> PackagedGoodsService::product(std::string_view beer, domain::PackageType type,
                                                                     double unit_volume_l) const {
    for (domain::PackagedProduct& product : domain::combine_by_beer(packaged_)) {
        if (product.type == type && product.unit_volume_l == unit_volume_l && same_name(product.beer, beer)) {
            return std::move(product);
        }
    }
    return std::nullopt;
}

void PackagedGoodsService::take(const domain::PackagedProduct& product, int count, domain::PackagedReason reason,
                                domain::Timestamp at, std::string_view note) {
    int left = count;
    for (const domain::PackagedStock& batch : product.batches) {
        const int taken = std::min(left, batch.count);
        if (taken <= 0) continue;
        record_packaged(batch.batch_id, product.type, product.unit_volume_l, -taken, reason, at, note);
        left -= taken;
    }
}

void PackagedGoodsService::remove(const PackagedRemoval& removal) {
    if (domain::adds_packages(removal.reason)) fail("That doesn't take beer out of packaged goods.");
    if (removal.count <= 0) fail("Enter how many were taken out.");
    const std::optional<domain::PackagedProduct> found = product(removal.beer, removal.type, removal.unit_volume_l);
    const int on_hand = found ? found->count : 0;
    if (removal.count > on_hand) {
        fail(std::string(on_hand == 1 ? "There is only " : "There are only ") +
             domain::count_text(removal.type, on_hand) + " of " + removal.beer + ".");
    }

    data::Transaction tx(db_);
    take(*found, removal.count, removal.reason, removal.at, removal.note);
    if (domain::is_keg(removal.type) && domain::keg_comes_back(removal.reason)) {
        record(removal.type, domain::KegState::Dirty, removal.count, domain::KegReason::Emptied, removal.at,
               std::nullopt, found->beer + ": " + std::string(domain::name(removal.reason)));
    }
    tx.commit();
    refresh();
}

void PackagedGoodsService::count(std::string_view beer, domain::PackageType type, double unit_volume_l, int counted,
                                 domain::Timestamp at, std::string_view note) {
    if (counted < 0) fail("A count can't be negative.");
    const std::optional<domain::PackagedProduct> found = product(beer, type, unit_volume_l);
    if (!found) fail("There is no " + std::string(beer) + " in that package to count.");
    const int change = counted - found->count;
    if (change == 0) return;

    data::Transaction tx(db_);
    if (change < 0) {
        take(*found, -change, domain::PackagedReason::Shortage, at, note);
    } else {
        record_packaged(found->batches.back().batch_id, type, unit_volume_l, change, domain::PackagedReason::Overage, at,
                        note);
    }
    tx.commit();
    refresh();
}

void PackagedGoodsService::return_packages(std::int64_t batch_id, domain::PackageType type, double unit_volume_l,
                                           int count, domain::Timestamp at, std::string_view note) {
    if (count <= 0) fail("Enter how many came back.");
    const std::vector<domain::PackagedStock> ever = repository_.packaged_ever(100000);
    const bool packaged = std::any_of(ever.begin(), ever.end(), [&](const domain::PackagedStock& entry) {
        return entry.batch_id == batch_id && entry.type == type && entry.unit_volume_l == unit_volume_l;
    });
    if (!packaged) fail("That batch was never packaged that way.");

    data::Transaction tx(db_);
    record_packaged(batch_id, type, unit_volume_l, count, domain::PackagedReason::Returned, at, note);
    tx.commit();
    refresh();
}

// --- Kegs -----------------------------------------------------------------------

void PackagedGoodsService::check(std::span<const domain::KegCount> kegs) {
    int total = 0;
    for (const domain::KegCount& entry : kegs) {
        if (!domain::is_keg(entry.type)) fail("Only kegs are kept in the keg cleaner.");
        if (entry.count < 0) fail("A number of kegs can't be negative.");
        total += entry.count;
    }
    if (total == 0) fail("Enter how many kegs.");
}

void PackagedGoodsService::add_kegs(std::span<const domain::KegCount> kegs, domain::Timestamp at,
                                    std::string_view note) {
    check(kegs);
    data::Transaction tx(db_);
    for (const domain::KegCount& entry : kegs) {
        if (entry.count > 0) {
            record(entry.type, domain::KegState::Dirty, entry.count, domain::KegReason::Added, at, std::nullopt, note);
        }
    }
    tx.commit();
    refresh();
}

void PackagedGoodsService::clean_kegs(std::span<const domain::KegCount> kegs, domain::Timestamp at) {
    check(kegs);
    for (const domain::KegCount& entry : kegs) {
        const int dirty = this->kegs(entry.type).dirty;
        if (entry.count > dirty) fail(only_text(dirty, domain::KegState::Dirty, entry.type));
    }
    data::Transaction tx(db_);
    for (const domain::KegCount& entry : kegs) {
        if (entry.count == 0) continue;
        record(entry.type, domain::KegState::Dirty, -entry.count, domain::KegReason::Cleaned, at, std::nullopt, {});
        record(entry.type, domain::KegState::Clean, entry.count, domain::KegReason::Cleaned, at, std::nullopt, {});
    }
    tx.commit();
    refresh();
}

void PackagedGoodsService::clean_all(domain::Timestamp at) {
    std::vector<domain::KegCount> dirty;
    for (domain::PackageType type : domain::kKegTypes) {
        if (kegs(type).dirty > 0) dirty.push_back({type, kegs(type).dirty});
    }
    if (!dirty.empty()) clean_kegs(dirty, at);
}

void PackagedGoodsService::remove_kegs(std::span<const domain::KegCount> kegs, domain::KegState state,
                                       domain::Timestamp at, std::string_view note) {
    check(kegs);
    for (const domain::KegCount& entry : kegs) {
        const domain::KegCounts held = this->kegs(entry.type);
        const int there = state == domain::KegState::Dirty ? held.dirty : held.clean;
        if (entry.count > there) fail(only_text(there, state, entry.type));
    }
    data::Transaction tx(db_);
    for (const domain::KegCount& entry : kegs) {
        if (entry.count > 0) record(entry.type, state, -entry.count, domain::KegReason::Removed, at, std::nullopt, note);
    }
    tx.commit();
    refresh();
}

void PackagedGoodsService::write_fill(domain::PackageType type, int count, std::int64_t batch_id,
                                      domain::Timestamp at) {
    const domain::KegDraw draw = domain::plan_keg_draw(repository_.keg_counts(type), count);
    if (draw.unrecorded > 0) {
        // Kegs the cleaner didn't know about: count them in, then fill them.
        record(type, domain::KegState::Clean, draw.unrecorded, domain::KegReason::Added, at, std::nullopt,
               "Filled, but not recorded in the keg cleaner");
    }
    if (draw.clean + draw.unrecorded > 0) {
        record(type, domain::KegState::Clean, -(draw.clean + draw.unrecorded), domain::KegReason::Filled, at,
               batch_id, {});
    }
    if (draw.dirty > 0) {
        record(type, domain::KegState::Dirty, -draw.dirty, domain::KegReason::Filled, at, batch_id,
               "Cleaned without being logged");
    }
}

void PackagedGoodsService::record_packaged(std::int64_t batch_id, domain::PackageType type, double unit_volume_l,
                                           int quantity, domain::PackagedReason reason, domain::Timestamp at,
                                           std::string_view note) {
    domain::PackagedMovement movement;
    movement.at = at;
    movement.batch_id = batch_id;
    movement.type = type;
    movement.unit_volume_l = unit_volume_l;
    movement.quantity = quantity;
    movement.reason = reason;
    movement.note = std::string(note);
    repository_.insert_packaged_movement(movement, session_.user_id);
}

void PackagedGoodsService::write_packaged(std::int64_t batch_id, std::optional<std::int64_t> vessel_id,
                                          domain::PackageType type, double unit_volume_l, int count,
                                          domain::Timestamp at, std::string_view note) {
    domain::PackagedMovement movement;
    movement.at = at;
    movement.batch_id = batch_id;
    movement.type = type;
    movement.unit_volume_l = unit_volume_l;
    movement.quantity = count;
    movement.reason = domain::PackagedReason::Packaged;
    movement.vessel_id = vessel_id;
    movement.note = std::string(note);
    repository_.insert_packaged_movement(movement, session_.user_id);
}

void PackagedGoodsService::refresh() {
    for (domain::PackageType type : domain::kKegTypes) kegs_[index_of(type)] = repository_.keg_counts(type);
    packaged_ = repository_.packaged_stock();
    ++revision_;
}

void PackagedGoodsService::record(domain::PackageType type, domain::KegState state, int quantity,
                                  domain::KegReason reason, domain::Timestamp at,
                                  std::optional<std::int64_t> batch_id, std::string_view note) {
    domain::KegMovement movement;
    movement.at = at;
    movement.type = type;
    movement.state = state;
    movement.quantity = quantity;
    movement.reason = reason;
    movement.batch_id = batch_id;
    movement.note = std::string(note);
    repository_.insert_keg_movement(movement, session_.user_id);
}

} // namespace ninkasi::app
