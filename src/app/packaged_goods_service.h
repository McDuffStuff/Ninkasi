#pragma once
// Packaged beer and the brewery's kegs.
//
// Packaged beer is counted in packages (half-barrels, sixtels, cans) by
// batch, in the packaged goods ledger (packaged_movements). Packaging (see
// BatchService) adds to it; beer leaves it when it's sold, sent to the
// taproom, exported, destroyed and so on, oldest batch first. Each reason is
// a line of the TTB report of operations.
//
// Empty kegs are kept in the keg cleaner, counted by size: kegs arrive dirty
// (new, or back from customers), are cleaned, and are filled when beer is
// packaged. Every change is a row in the keg ledger (keg_movements); the
// counts are its sums. Packaging (see BatchService) takes clean kegs first;
// if the brewer packaged more than the cleaner says are clean, the rest were
// cleaned without it being logged, or weren't recorded at all, and the
// ledger says so rather than refusing the packaging.
//
// Changes run in one transaction each. What's on hand is kept in memory for
// the floor.

#include "app/session.h"
#include "data/database.h"
#include "data/packaging_repository.h"
#include "domain/packaging.h"
#include "domain/timestamp.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace ninkasi::app {

// Packages of one beer, in one kind and size of package (a row of
// domain::combine_by_beer), taken out of packaged goods.
struct PackagedRemoval {
    std::string beer;
    domain::PackageType type = domain::PackageType::HalfBarrel;
    double unit_volume_l = 0.0;
    int count = 0;
    domain::PackagedReason reason = domain::PackagedReason::Sold;
    domain::Timestamp at{};
    std::string note;
};

class PackagedGoodsService {
public:
    PackagedGoodsService(data::Database& db, Session session);

    // --- In memory ------------------------------------------------------------

    // Empty kegs of one size.
    domain::KegCounts kegs(domain::PackageType type) const;
    // Packaged beer on hand, oldest first.
    const std::vector<domain::PackagedStock>& packaged() const { return packaged_; }
    // Grows with every change, so windows know when to re-read the database.
    std::uint64_t revision() const { return revision_; }

    // --- From the database ----------------------------------------------------

    std::vector<data::KegMovementRow> keg_history(int limit = 500);
    std::vector<data::PackagedMovementRow> packaged_history(int limit = 500);
    // Every batch and package ever packaged, newest first, for returns.
    std::vector<domain::PackagedStock> packaged_ever(int limit = 200);

    // --- Packaged beer --------------------------------------------------------
    // Each throws std::invalid_argument, saving nothing, if it can't be done.

    // Takes packages out, oldest batch first. Kegs emptied at the brewery
    // (see domain::keg_comes_back) go back to the keg cleaner dirty. Throws
    // if there aren't that many.
    void remove(const PackagedRemoval& removal);
    // A stock count of one beer in one package: a shortage comes out oldest
    // batch first, an overage goes onto the newest batch.
    void count(std::string_view beer, domain::PackageType type, double unit_volume_l, int counted,
               domain::Timestamp at, std::string_view note);
    // Packages of a batch returned to the brewery after they were removed.
    void return_packages(std::int64_t batch_id, domain::PackageType type, double unit_volume_l, int count,
                         domain::Timestamp at, std::string_view note);

    // --- Kegs -----------------------------------------------------------------
    // Each throws std::invalid_argument, saving nothing, if no kegs are given,
    // a count is negative or a type isn't a keg.

    // New kegs, or kegs back from customers. They arrive dirty.
    void add_kegs(std::span<const domain::KegCount> kegs, domain::Timestamp at, std::string_view note);
    // Throws if more are cleaned than are dirty.
    void clean_kegs(std::span<const domain::KegCount> kegs, domain::Timestamp at);
    // Cleans every dirty keg. Does nothing if none are dirty.
    void clean_all(domain::Timestamp at);
    // Lost, damaged or retired kegs. Throws if more are removed than there are.
    void remove_kegs(std::span<const domain::KegCount> kegs, domain::KegState state, domain::Timestamp at,
                     std::string_view note);

    // For packaging: takes `count` kegs of `type` for `batch_id`, clean ones
    // first (see domain::plan_keg_draw). Call inside the caller's
    // transaction, and refresh() once it's committed.
    void write_fill(domain::PackageType type, int count, std::int64_t batch_id, domain::Timestamp at);
    // For packaging: adds `count` packages of a batch, each holding
    // `unit_volume_l`, filled from `vessel_id`. Call inside the caller's
    // transaction, and refresh() once it's committed.
    void write_packaged(std::int64_t batch_id, std::optional<std::int64_t> vessel_id, domain::PackageType type,
                        double unit_volume_l, int count, domain::Timestamp at, std::string_view note);

    // Re-reads what's on hand.
    void refresh();

private:
    // Throws unless the kegs are a sensible request: keg types, no negative
    // counts, at least one keg.
    static void check(std::span<const domain::KegCount> kegs);
    void record(domain::PackageType type, domain::KegState state, int quantity, domain::KegReason reason,
                domain::Timestamp at, std::optional<std::int64_t> batch_id, std::string_view note);
    // A row of the packaged goods ledger.
    void record_packaged(std::int64_t batch_id, domain::PackageType type, double unit_volume_l, int quantity,
                         domain::PackagedReason reason, domain::Timestamp at, std::string_view note);
    // The product on hand matching a beer and package, if there is any.
    std::optional<domain::PackagedProduct> product(std::string_view beer, domain::PackageType type,
                                                   double unit_volume_l) const;
    // Writes `count` packages of a product out, oldest batch first.
    void take(const domain::PackagedProduct& product, int count, domain::PackagedReason reason, domain::Timestamp at,
              std::string_view note);

    data::Database& db_;
    Session session_;
    data::PackagingRepository repository_;
    std::array<domain::KegCounts, domain::kPackageTypes.size()> kegs_{};  // by PackageType
    std::vector<domain::PackagedStock> packaged_;
    std::uint64_t revision_ = 0;
};

} // namespace ninkasi::app
