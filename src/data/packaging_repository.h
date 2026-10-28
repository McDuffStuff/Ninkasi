#pragma once
// Reads and writes the keg ledger. PackagedGoodsService decides what may be
// written; this only stores it.

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

class PackagingRepository {
public:
    explicit PackagingRepository(Database& db) : db_(db) {}

    // --- Kegs ---------------------------------------------------------------
    void insert_keg_movement(const domain::KegMovement& movement, std::int64_t user_id);
    // Empty kegs of one size.
    domain::KegCounts keg_counts(domain::PackageType type);
    // Newest first.
    std::vector<KegMovementRow> keg_movements(int limit);

private:
    Database& db_;
};

} // namespace ninkasi::data
