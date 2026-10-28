#include "app/backup_service.h"

#include "data/migrations.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace ninkasi::app {

namespace fs = std::filesystem;

namespace {

constexpr const char* kAutomaticKey = "backup.automatic";
constexpr const char* kKeepKey = "backup.keep";
constexpr const char* kFolderKey = "backup.folder";
constexpr const char* kPrefix = "ninkasi-";               // backups made here
constexpr const char* kBeforeRestorePrefix = "before-restore-";

// Paths as SQLite and the settings table store them: UTF-8.
std::string utf8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

fs::path from_utf8(const std::string& text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

// The file that asks for a restore at the next start, beside the database.
fs::path restore_marker(const fs::path& db_path) {
    fs::path marker = db_path;
    marker += ".restore";
    return marker;
}

fs::path default_folder(const fs::path& db_path) {
    return db_path.parent_path() / "backups";
}

// "2026-10-28-0815", in local time, so backups sort by when they were made.
std::string stamp(domain::Timestamp at) {
    const domain::LocalTime local = domain::to_local(at);
    char text[32];
    std::snprintf(text, sizeof text, "%04d-%02d-%02d-%02d%02d", local.year, local.month, local.day, local.hour,
                  local.minute);
    return text;
}

// `folder`/`name`.db, or `name`-2.db and so on if that's taken.
fs::path unused_path(const fs::path& folder, const std::string& name) {
    fs::path path = folder / (name + ".db");
    for (int n = 2; fs::exists(path); ++n) path = folder / (name + "-" + std::to_string(n) + ".db");
    return path;
}

void make_folder(const fs::path& folder) {
    std::error_code error;
    fs::create_directories(folder, error);
    if (error) throw std::runtime_error("Couldn't create the backup folder " + utf8(folder) + ": " + error.message());
}

bool starts_with(const fs::path& path, const char* prefix) {
    return utf8(path.filename()).starts_with(prefix) && path.extension() == ".db";
}

domain::Timestamp modified(const fs::path& path) {
    const auto time = std::chrono::clock_cast<std::chrono::system_clock>(fs::last_write_time(path));
    return std::chrono::floor<std::chrono::seconds>(time);
}

} // namespace

void write_backup(data::Database& db, const fs::path& path) {
    try {
        auto stmt = db.prepare("VACUUM INTO ?;");
        stmt.bind(1, utf8(path));
        stmt.execute();
    } catch (const std::exception& e) {
        throw std::runtime_error("Couldn't write the backup " + utf8(path) + ": " + e.what());
    }
}

BackupService::BackupService(data::Database& db, data::SettingsRepository& settings, fs::path db_path)
    : db_(db), settings_(settings), db_path_(std::move(db_path)) {
    automatic_ = settings_.get_or(kAutomaticKey, "1") != "0";
    try {
        keep_ = std::max(1, std::stoi(settings_.get_or(kKeepKey, "14")));
    } catch (const std::exception&) {
        keep_ = 14;
    }
    const std::string folder = settings_.get_or(kFolderKey, "");
    folder_ = folder.empty() ? default_folder(db_path_) : from_utf8(folder);
}

void BackupService::set_automatic(bool automatic) {
    automatic_ = automatic;
    settings_.set(kAutomaticKey, automatic ? "1" : "0");
}

void BackupService::set_keep(int keep) {
    keep_ = std::max(1, keep);
    settings_.set(kKeepKey, std::to_string(keep_));
}

void BackupService::set_folder(const fs::path& folder) {
    folder_ = folder;
    settings_.set(kFolderKey, utf8(folder));
}

fs::path BackupService::back_up_now() {
    make_folder(folder_);
    const fs::path path = unused_path(folder_, kPrefix + stamp(domain::now()));
    write_backup(db_, path);
    prune();
    return path;
}

void BackupService::save_copy(const fs::path& path) {
    std::error_code error;
    fs::remove(path, error);  // VACUUM INTO won't write over a file
    write_backup(db_, path);
}

std::optional<fs::path> BackupService::back_up_if_due() {
    if (!automatic_) return std::nullopt;
    const domain::Date today = domain::today();
    for (const BackupFile& backup : backups()) {
        if (starts_with(backup.path, kPrefix) && domain::local_date(backup.made) == today) return std::nullopt;
    }
    return back_up_now();
}

std::vector<BackupFile> BackupService::backups() const {
    std::vector<BackupFile> files;
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder_, error)) {
        if (!entry.is_regular_file()) continue;
        if (!starts_with(entry.path(), kPrefix) && !starts_with(entry.path(), kBeforeRestorePrefix)) continue;
        files.push_back({entry.path(), modified(entry.path()), entry.file_size()});
    }
    std::sort(files.begin(), files.end(), [](const BackupFile& a, const BackupFile& b) { return a.made > b.made; });
    return files;
}

void BackupService::prune() {
    // Names without ".db" sort in the order they were made ("...-0815" before
    // "...-0815-2"); the "before restore" copies are never removed.
    std::vector<fs::path> made_here;
    for (const BackupFile& backup : backups()) {
        if (starts_with(backup.path, kPrefix)) made_here.push_back(backup.path);
    }
    std::sort(made_here.begin(), made_here.end(),
              [](const fs::path& a, const fs::path& b) { return a.stem() > b.stem(); });
    for (std::size_t i = static_cast<std::size_t>(keep_); i < made_here.size(); ++i) {
        std::error_code error;
        fs::remove(made_here[i], error);
    }
}

void BackupService::restore_on_next_start(const fs::path& backup) {
    if (!fs::exists(backup)) throw std::runtime_error("There is no backup at " + utf8(backup) + ".");
    // The backup to restore, then where to save the data it replaces.
    std::ofstream marker(restore_marker(db_path_), std::ios::binary | std::ios::trunc);
    marker << utf8(backup) << '\n' << utf8(folder_) << '\n';
    if (!marker) throw std::runtime_error("Couldn't arrange the restore.");
}

std::optional<fs::path> BackupService::pending_restore() const {
    std::ifstream marker(restore_marker(db_path_), std::ios::binary);
    std::string backup;
    if (!marker || !std::getline(marker, backup) || backup.empty()) return std::nullopt;
    return from_utf8(backup);
}

void BackupService::cancel_restore() {
    std::error_code error;
    fs::remove(restore_marker(db_path_), error);
}

std::optional<std::string> restore_pending_backup(const fs::path& db_path) {
    const fs::path marker_path = restore_marker(db_path);
    if (!fs::exists(marker_path)) return std::nullopt;
    std::string backup_text;
    std::string folder_text;
    {
        std::ifstream marker(marker_path, std::ios::binary);
        std::getline(marker, backup_text);
        std::getline(marker, folder_text);
    }
    // Asked for once: whatever happens, don't try again next time.
    fs::remove(marker_path);
    const fs::path backup = from_utf8(backup_text);
    const fs::path folder = folder_text.empty() ? default_folder(db_path) : from_utf8(folder_text);

    if (!fs::exists(backup)) throw std::runtime_error("The backup to restore, " + utf8(backup) + ", is gone.");

    // Check a copy of the backup (opening a database can change it), which
    // then becomes the brewery's database.
    const fs::path incoming = fs::path(db_path).concat(".restoring");
    std::error_code error;
    fs::copy_file(backup, incoming, fs::copy_options::overwrite_existing);
    try {
        data::Database check(utf8(incoming));
        if (data::schema_version(check) <= 0) throw std::runtime_error(utf8(backup) + " isn't a Ninkasi database.");
    } catch (const std::exception&) {
        fs::remove(incoming, error);
        throw;
    }

    // Keep the data being replaced, with anything not yet checkpointed from its log.
    fs::path saved;
    if (fs::exists(db_path)) {
        make_folder(folder);
        saved = unused_path(folder, kBeforeRestorePrefix + stamp(domain::now()));
        data::Database current(utf8(db_path));
        write_backup(current, saved);
    }
    fs::remove(fs::path(db_path).concat("-wal"), error);
    fs::remove(fs::path(db_path).concat("-shm"), error);
    fs::rename(incoming, db_path);

    std::string message = "Restored the backup " + utf8(backup.filename()) + ".";
    if (!saved.empty()) message += " The data it replaced was saved as " + utf8(saved.filename()) + ".";
    return message;
}

} // namespace ninkasi::app
