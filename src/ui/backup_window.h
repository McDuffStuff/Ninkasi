#pragma once
// File > Backups and export: backing the database up (now, as a copy saved
// anywhere, or automatically every day), restoring a backup, and exporting
// the records as CSV files for a spreadsheet.

#include "app/backup_service.h"
#include "app/export_service.h"

#include <imgui.h>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace ninkasi::ui {

class BackupWindow {
public:
    BackupWindow(app::BackupService& backups, app::ExportService& exports, float ui_scale);

    void show();
    bool* visible() { return &visible_; }
    void draw();

    // Backs up into the backup folder now, saying how it went in the window.
    void back_up_now();

    // Called after a restore is arranged, so Ninkasi can close.
    std::function<void()> on_quit;

private:
    void draw_backups();
    void draw_export();
    void draw_restore_confirmation();
    void refresh();

    app::BackupService& backups_;
    app::ExportService& exports_;
    float ui_scale_;

    bool visible_ = false;
    bool focus_ = false;
    std::vector<app::BackupFile> files_;  // in the folder, re-read when something changes
    bool stale_ = true;
    std::string backup_status_;
    std::string export_status_;
    std::filesystem::path export_folder_;  // where everything was last exported
    int keep_ = 14;                        // being edited
    std::filesystem::path restore_;        // asked to restore, waiting for confirmation
    bool confirm_restore_ = false;
};

} // namespace ninkasi::ui
