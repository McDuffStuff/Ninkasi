#pragma once
// Exports the brewery's records as CSV files for spreadsheets: one file per
// table (batches, brews, readings, the volume ledger, inventory, invoices,
// packaged goods, logs...), with names instead of ids. Beer volumes,
// gravities and temperatures are in the brewery's units; inventory amounts
// are in kilograms, liters or counts, with a unit column; times are local.

#include "calc/units.h"
#include "data/database.h"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ninkasi::app {

struct ExportTable {
    std::string_view key;          // for csv() and save()
    std::string_view name;         // shown, and the file's name: "Batches"
    std::string_view description;  // shown beside it
};

class ExportService {
public:
    ExportService(data::Database& db, const calc::UnitPreferences& units);

    // Everything that can be exported, in the order it's listed.
    static std::span<const ExportTable> tables();

    // A table as CSV. Throws std::invalid_argument for an unknown key.
    std::string csv(std::string_view key);
    // Writes a table to `path`. Throws std::runtime_error if it can't.
    void save(std::string_view key, const std::filesystem::path& path);
    // Writes every table into `folder` as "<name>.csv"; returns the files.
    std::vector<std::filesystem::path> save_all(const std::filesystem::path& folder);

private:
    data::Database& db_;
    const calc::UnitPreferences& units_;
};

} // namespace ninkasi::app
