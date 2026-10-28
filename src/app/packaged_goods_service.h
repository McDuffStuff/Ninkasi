#pragma once
// Packaged beer and the brewery's kegs.
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

class PackagedGoodsService {
public:
    PackagedGoodsService(data::Database& db, Session session);

    // --- In memory ------------------------------------------------------------

    // Empty kegs of one size.
    domain::KegCounts kegs(domain::PackageType type) const;
    // Grows with every change, so windows know when to re-read the database.
    std::uint64_t revision() const { return revision_; }

    // --- From the database ----------------------------------------------------

    std::vector<data::KegMovementRow> keg_history(int limit = 500);

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

    // Re-reads what's on hand.
    void refresh();

private:
    // Throws unless the kegs are a sensible request: keg types, no negative
    // counts, at least one keg.
    static void check(std::span<const domain::KegCount> kegs);
    void record(domain::PackageType type, domain::KegState state, int quantity, domain::KegReason reason,
                domain::Timestamp at, std::optional<std::int64_t> batch_id, std::string_view note);

    data::Database& db_;
    Session session_;
    data::PackagingRepository repository_;
    std::array<domain::KegCounts, domain::kPackageTypes.size()> kegs_{};  // by PackageType
    std::uint64_t revision_ = 0;
};

} // namespace ninkasi::app
