#include "ui/backup_window.h"

#include "ui/display.h"
#include "ui/file_dialog.h"
#include "ui/widgets.h"

#include <cstdio>
#include <exception>

namespace ninkasi::ui {

namespace {

namespace fs = std::filesystem;

constexpr const char* kRestorePopup = "Restore a backup?";

constexpr ImGuiTableFlags kListFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                       ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;

// A path as text to show.
std::string shown(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

// "340 KB", "1.2 MB".
std::string size_text(std::uintmax_t bytes) {
    char text[32];
    if (bytes < 1024 * 1024) {
        std::snprintf(text, sizeof text, "%.0f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(text, sizeof text, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    return text;
}

} // namespace

BackupWindow::BackupWindow(app::BackupService& backups, app::ExportService& exports, float ui_scale)
    : backups_(backups), exports_(exports), ui_scale_(ui_scale) {}

void BackupWindow::show() {
    visible_ = true;
    focus_ = true;
    stale_ = true;
}

void BackupWindow::refresh() {
    files_ = backups_.backups();
    keep_ = backups_.keep();
    stale_ = false;
}

void BackupWindow::back_up_now() {
    try {
        const fs::path made = backups_.back_up_now();
        backup_status_ = "Backed up to " + shown(made.filename()) + ".";
    } catch (const std::exception& e) {
        backup_status_ = e.what();
    }
    stale_ = true;
}

void BackupWindow::draw() {
    if (!visible_) return;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(760.0f, 560.0f) * ui_scale_, ImGuiCond_FirstUseEver);
    const bool raise = focus_;
    if (focus_) {
        ImGui::SetNextWindowFocus();
        focus_ = false;
    }
    const bool expanded = ImGui::Begin("Backups and export", &visible_);
    if (raise) raise_os_window();
    if (expanded) {
        if (stale_) refresh();
        if (ImGui::BeginTabBar("##backup_tabs")) {
            if (ImGui::BeginTabItem("Backups")) {
                draw_backups();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Export")) {
                draw_export();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        draw_restore_confirmation();
    }
    ImGui::End();
}

void BackupWindow::draw_backups() {
    if (ImGui::Button("Back up now")) back_up_now();
    ImGui::SameLine();
    if (ImGui::Button("Save a copy...")) {
        const std::string name = "ninkasi-" + date_text(domain::today()) + ".db";
        if (const std::optional<fs::path> path =
                save_file_dialog("Save a copy of the brewery's data", "Ninkasi data", "*.db", "db", name.c_str())) {
            try {
                backups_.save_copy(*path);
                backup_status_ = "Saved a copy to " + shown(*path) + ".";
            } catch (const std::exception& e) {
                backup_status_ = e.what();
            }
        }
    }
    ImGui::SetItemTooltip("A backup saved wherever you like: another drive, a shared folder...");
    if (!backup_status_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", backup_status_.c_str());
    }
    ImGui::Spacing();
    // Anyone can back up; the settings and restoring are for admins.
    const bool admin = backups_.can_change_settings();
    if (!admin) ImGui::TextDisabled("Only an admin can change these settings or restore a backup.");

    if (ImGui::BeginTable("##backup_settings", 2, ImGuiTableFlags_SizingFixedFit)) {
        // The second column reaches the window's edge, so the folder wraps there.
        ImGui::TableSetupColumn("##label");
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::BeginDisabled(!admin);
        table_row("Every day");
        bool automatic = backups_.automatic();
        if (ImGui::Checkbox("Back up when Ninkasi starts, once a day", &automatic)) backups_.set_automatic(automatic);
        table_row("Keep");
        ImGui::SetNextItemWidth(60.0f * ui_scale_);
        ImGui::InputInt("##keep", &keep_, 0, 0, ImGuiInputTextFlags_AutoSelectAll);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            backups_.set_keep(keep_);
            stale_ = true;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("backups; older ones are removed");
        // The buttons first: a long folder path wraps after them.
        table_row("Folder");
        if (ImGui::SmallButton("Change...")) {
            if (const std::optional<fs::path> folder =
                    choose_folder_dialog("Choose where to keep backups", backups_.folder())) {
                backups_.set_folder(*folder);
                stale_ = true;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("Open")) {
            std::error_code error;
            fs::create_directories(backups_.folder(), error);
            show_folder(backups_.folder());
        }
        ImGui::SameLine();
        ImGui::TextWrapped("%s", shown(backups_.folder()).c_str());
        ImGui::EndTable();
    }

    if (const std::optional<fs::path> pending = backups_.pending_restore()) {
        ImGui::Spacing();
        ImGui::TextColored(kWarningColor, "%s will be restored the next time Ninkasi starts.",
                           shown(pending->filename()).c_str());
        if (admin) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Cancel the restore")) backups_.cancel_restore();
        }
    }

    ImGui::SeparatorText("Backups in the folder");
    if (files_.empty()) {
        wrapped_note("No backups yet. Back up now makes one; with Every day on, one is made each day you use "
                     "Ninkasi.");
        return;
    }
    if (!ImGui::BeginTable("##backups", 4, kListFlags | ImGuiTableFlags_ScrollY)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Made");
    ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Size");
    ImGui::TableSetupColumn("##restore");
    ImGui::TableHeadersRow();
    for (std::size_t i = 0; i < files_.size(); ++i) {
        const app::BackupFile& file = files_[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(time_text(file.made).c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(shown(file.path.filename()).c_str());
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", size_text(file.bytes).c_str());
        ImGui::TableNextColumn();
        if (admin && ImGui::SmallButton("Restore...")) {
            restore_ = file.path;
            confirm_restore_ = true;
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void BackupWindow::draw_restore_confirmation() {
    if (confirm_restore_) {
        ImGui::OpenPopup(kRestorePopup);
        confirm_restore_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetWindowViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(kRestorePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("Restore %s?", shown(restore_.filename()).c_str());
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 420.0f * ui_scale_);
    ImGui::TextDisabled("Ninkasi will close. When you start it again, this backup replaces all of the brewery's "
                        "current data. The current data is saved as a backup first, so you can go back to it.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button("Restore and close")) {
        try {
            backups_.restore_on_next_start(restore_);
            ImGui::CloseCurrentPopup();
            if (on_quit) on_quit();
        } catch (const std::exception& e) {
            backup_status_ = e.what();
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || dialog_escape_pressed()) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void BackupWindow::draw_export() {
    wrapped_note("Save the brewery's records as CSV files to open in a spreadsheet. Beer volumes, gravities and "
                 "temperatures are in your units, and times are local.");
    ImGui::Spacing();
    if (ImGui::Button("Export everything to a folder...")) {
        if (const std::optional<fs::path> folder =
                choose_folder_dialog("Choose a folder for the CSV files", export_folder_)) {
            try {
                const std::vector<fs::path> files = exports_.save_all(*folder);
                export_folder_ = *folder;
                export_status_ = "Saved " + std::to_string(files.size()) + " files to " + shown(*folder) + ".";
            } catch (const std::exception& e) {
                export_status_ = e.what();
            }
        }
    }
    if (!export_folder_.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Open the folder")) show_folder(export_folder_);
    }
    if (!export_status_.empty()) ImGui::TextDisabled("%s", export_status_.c_str());
    ImGui::Spacing();

    if (!ImGui::BeginTable("##tables", 3, kListFlags | ImGuiTableFlags_ScrollY)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Table");
    ImGui::TableSetupColumn("What's in it", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("##export");
    ImGui::TableHeadersRow();
    for (const app::ExportTable& table : app::ExportService::tables()) {
        ImGui::PushID(table.key.data());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(table.name.data());
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", table.description.data());
        ImGui::TableNextColumn();
        if (ImGui::SmallButton("Export...")) {
            const std::string name = std::string(table.name) + ".csv";
            if (const std::optional<fs::path> path =
                    save_file_dialog("Export to CSV", "CSV files", "*.csv", "csv", name.c_str())) {
                try {
                    exports_.save(table.key, *path);
                    export_status_ = "Saved " + shown(*path) + ".";
                } catch (const std::exception& e) {
                    export_status_ = e.what();
                }
            }
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

} // namespace ninkasi::ui
