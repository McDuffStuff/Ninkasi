#pragma once
// Reads and writes the brewery's adjusted formulas. The originals are part of
// Ninkasi (calc/formulas.h); only adjustments are stored, so deleting a row
// puts that formula back.

#include "data/database.h"
#include "domain/timestamp.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ninkasi::data {

struct StoredFormula {
    std::string key;    // calc::FormulaInfo::key
    std::string latex;  // the adjusted formula
    std::string by;     // who adjusted it
    domain::Timestamp at{};
};

class FormulaRepository {
public:
    explicit FormulaRepository(Database& db) : db_(db) {}

    std::vector<StoredFormula> load_all();
    // Saves `latex` as the formula with `key`, replacing any earlier adjustment.
    void save(std::string_view key, std::string_view latex, std::int64_t user_id, domain::Timestamp at);
    void remove(std::string_view key);
    void remove_all();

private:
    Database& db_;
};

} // namespace ninkasi::data
