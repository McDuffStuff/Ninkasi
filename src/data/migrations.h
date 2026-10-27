#pragma once
// Schema migrations.
//
// The schema version lives in SQLite's built-in `PRAGMA user_version`.
// Each migration runs in its own transaction, so a failure leaves the database
// at the last good version. Never edit a migration once it has shipped —
// add a new one instead.

#include "data/database.h"

namespace ninkasi::data {

// The schema version this build of Ninkasi expects.
int latest_schema_version();

// Returns the schema version currently stored in the database.
int schema_version(Database& db);

// Applies every migration newer than the database's current version.
void migrate(Database& db);

} // namespace ninkasi::data
