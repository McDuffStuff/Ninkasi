#pragma once
// Reads and writes the keg and packaged goods ledgers. PackagedGoodsService
// decides what may be written; this only stores it.

#include "data/database.h"
#include "domain/packaging.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ninkasi::data {

// A keg ledger row with the code of the batch the kegs were filled with.
struct KegMovementRow {
    domain::KegMovement movement;
    std::string batch_code;
};

// A packaged goods ledger row with its beer and the tank it came from.
struct PackagedMovementRow {
    domain::PackagedMovement movement;
    std::string batch_code;
    std::string beer;
    std::string vessel_name;
};

class PackagingRepository {
public:
    explicit PackagingRepository(Database& db) : db_(db) {}

    // --- Kegs ---------------------------------------------------------------
    void insert_keg_movement(const domain::KegMovement& movement, std::int64_t user_id);
    // Empty kegs of one size.
    domain::KegCounts keg_counts(domain::PackageType type);
    // Newest first.
    std::vector<KegMovementRow> keg_movements(int limit);

    // --- Packaged beer ------------------------------------------------------
    void insert_packaged_movement(const domain::PackagedMovement& movement, std::int64_t user_id);
    // What's on hand, oldest first.
    std::vector<domain::PackagedStock> packaged_stock();
    // Every batch and package ever packaged, newest first, with how many
    // were packaged (for returns).
    std::vector<domain::PackagedStock> packaged_ever(int limit);
    // Newest first.
    std::vector<PackagedMovementRow> packaged_movements(int limit);

private:
    Database& db_;
};

} // namespace ninkasi::data
