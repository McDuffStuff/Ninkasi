#pragma once
// Backups of the brewery's database: made by hand (Back up now, Save a copy)
// and automatically once a day when Ninkasi starts. A backup is a complete
// database file written by SQLite's VACUUM INTO, with everything saved so
// far, so it can be opened directly (ninkasi --db <backup>) or restored.
//
// Backups made here go in the backup folder ("backups" beside the database
// unless the brewer picks another), named for when they were made:
// "ninkasi-2026-10-28-0815.db". Only the newest few are kept.
//
// The database can't be replaced while it's open, so restoring happens the
// next time Ninkasi starts (restore_pending_backup), after the data being
// replaced is saved as one more backup.

#include "app/session.h"
#include "data/database.h"
#include "data/settings_repository.h"
#include "domain/timestamp.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ninkasi::app {

struct BackupFile {
    std::filesystem::path path;
    domain::Timestamp made{};  // when the file was written
    std::uintmax_t bytes = 0;
};

class BackupService {
public:
    BackupService(data::Database& db, Session session, data::SettingsRepository& settings,
                  std::filesystem::path db_path);

    // --- Settings, saved as they change; admins only (NotAllowed otherwise) ----------
    bool automatic() const { return automatic_; }
    void set_automatic(bool automatic);
    // How many backups to keep in the folder; at least 1.
    int keep() const { return keep_; }
    void set_keep(int keep);
    const std::filesystem::path& folder() const { return folder_; }
    void set_folder(const std::filesystem::path& folder);

    // --- Backing up ---------------------------------------------------------------
    // Each throws std::runtime_error if the backup can't be written.

    // Backs up into the folder, then removes the oldest beyond keep(). Returns the new file.
    std::filesystem::path back_up_now();
    // Writes a backup to `path`, replacing any file there.
    void save_copy(const std::filesystem::path& path);
    // At startup: backs up into the folder if automatic backups are on and
    // none has been made today. Returns the new file, if one was made.
    std::optional<std::filesystem::path> back_up_if_due();
    // The backups in the folder, newest first.
    std::vector<BackupFile> backups() const;

    // --- Restoring: admins only (NotAllowed otherwise) ---------------------------------
    // Asks for `backup` to replace the brewery's data the next time Ninkasi starts.
    void restore_on_next_start(const std::filesystem::path& backup);
    std::optional<std::filesystem::path> pending_restore() const;
    void cancel_restore();

private:
    // Removes the oldest backups in the folder beyond keep().
    void prune();

    data::Database& db_;
    Session session_;
    data::SettingsRepository& settings_;
    std::filesystem::path db_path_;
    bool automatic_ = true;
    int keep_ = 14;
    std::filesystem::path folder_;
};

// Writes a complete copy of `db` to `path` (which must not exist).
void write_backup(data::Database& db, const std::filesystem::path& path);

// At startup, before the database at `db_path` is opened: if a restore was
// asked for, saves the current data into the backup folder as
// "before-restore-....db" and puts the backup in its place. Returns what
// happened, for the brewer, or nullopt if no restore was waiting. Throws
// std::runtime_error, leaving the database as it was, if the backup is gone
// or isn't a Ninkasi database. Either way it's tried only once.
std::optional<std::string> restore_pending_backup(const std::filesystem::path& db_path);

} // namespace ninkasi::app
