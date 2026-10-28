#include "ui/people_window.h"

#include "ui/display.h"
#include "ui/search_combo.h"
#include "ui/widgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <exception>

namespace ninkasi::ui {

namespace {

// One popup for every dialog here; the text before ### is its title.
constexpr const char* kDialogId = "###person_dialog";

constexpr ImGuiTableFlags kListFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                       ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;

bool is_blank(const std::string& text) {
    return text.find_first_not_of(" \t") == std::string::npos;
}

} // namespace

PeopleWindow::PeopleWindow(app::UserService& users, const app::Session& session, float ui_scale)
    : users_(users), session_(session), ui_scale_(ui_scale) {}

void PeopleWindow::show() {
    visible_ = true;
    focus_ = true;
    stale_ = true;
}

void PeopleWindow::change_my_password() {
    stale_ = true;
    open_dialog(Dialog::OwnPassword, session_.user_id);
}

const domain::User* PeopleWindow::find(std::int64_t id) const {
    const auto found =
        std::find_if(people_.begin(), people_.end(), [id](const domain::User& person) { return person.id == id; });
    return found == people_.end() ? nullptr : &*found;
}

void PeopleWindow::open_dialog(Dialog dialog, std::int64_t person_id) {
    if (stale_) {
        people_ = users_.people();
        stale_ = false;
    }
    dialog_ = dialog;
    open_popup_ = true;
    person_id_ = person_id;
    const domain::User* person = find(person_id);
    name_ = person ? person->name : "";
    role_ = person ? person->role : domain::Role::Brewer;
    active_ = person ? person->active : true;
    remove_password_ = false;
    current_password_.clear();
    password_.clear();
    again_.clear();
    error_.clear();
}

void PeopleWindow::close_dialog() {
    dialog_ = Dialog::None;
    // Typed passwords shouldn't linger in memory.
    current_password_.clear();
    password_.clear();
    again_.clear();
    ImGui::CloseCurrentPopup();
}

void PeopleWindow::draw() {
    if (visible_) {
        if (stale_) {
            people_ = users_.people();
            stale_ = false;
        }
        ImGui::SetNextWindowSize(ImVec2(680.0f, 440.0f) * ui_scale_, ImGuiCond_FirstUseEver);
        const bool raise = focus_;
        if (focus_) {
            ImGui::SetNextWindowFocus();
            focus_ = false;
        }
        const bool expanded = ImGui::Begin("People", &visible_);
        if (raise) raise_os_window();
        if (expanded) draw_list();
        ImGui::End();
    }
    draw_dialog();
}

void PeopleWindow::draw_list() {
    wrapped_note("Admins can do everything. Brewers log the brewery's work, but don't manage people, set up "
                 "equipment, change the brewery's settings or restore backups. Passwords decide who can do what "
                 "in Ninkasi; they don't encrypt the data, which anyone who can open the database file can read.");
    ImGui::Spacing();

    if (ImGui::Button("Add person...")) open_dialog(Dialog::Add);
    if (!status_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", status_.c_str());
    }
    const domain::User* me = find(session_.user_id);
    if (me && !me->has_password) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kWarningColor, "You have no password. Set one before adding people, so no one else can "
                                          "sign in as you.");
        ImGui::PopTextWrapPos();
        if (ImGui::SmallButton("Set my password...")) open_dialog(Dialog::OwnPassword, session_.user_id);
    }
    ImGui::Spacing();

    if (!ImGui::BeginTable("##people", 5, kListFlags | ImGuiTableFlags_ScrollY)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Role");
    ImGui::TableSetupColumn("Password");
    ImGui::TableSetupColumn("Last signed in");
    ImGui::TableSetupColumn("##edit");
    ImGui::TableHeadersRow();
    for (const domain::User& person : people_) {
        ImGui::PushID(static_cast<int>(person.id));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const std::string name = person.name + (person.id == session_.user_id ? " (you)" : "");
        if (person.active) {
            ImGui::TextUnformatted(name.c_str());
        } else {
            ImGui::TextDisabled("%s (inactive)", name.c_str());
        }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(domain::name(person.role).data());
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", person.has_password ? "Set" : "None");
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", person.last_sign_in ? time_text(*person.last_sign_in).c_str() : "Never");
        ImGui::TableNextColumn();
        if (ImGui::SmallButton("Edit...")) open_dialog(Dialog::Edit, person.id);
        ImGui::PopID();
    }
    ImGui::EndTable();
}

// --- Dialogs ---------------------------------------------------------------------------------

void PeopleWindow::draw_dialog() {
    if (dialog_ == Dialog::None) return;
    const char* title = dialog_ == Dialog::Add    ? "Add a person"
                        : dialog_ == Dialog::Edit ? "Edit a person"
                                                  : "Change my password";
    const std::string popup = std::string(title) + kDialogId;
    if (open_popup_) {
        ImGui::OpenPopup(popup.c_str());
        open_popup_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    bool keep_open = true;
    if (!ImGui::BeginPopupModal(popup.c_str(), &keep_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        // Closed with its X.
        dialog_ = Dialog::None;
        current_password_.clear();
        password_.clear();
        again_.clear();
        return;
    }
    switch (dialog_) {
    case Dialog::Add:         draw_add(); break;
    case Dialog::Edit:        draw_edit(); break;
    case Dialog::OwnPassword: draw_own_password(); break;
    case Dialog::None:        break;
    }
    ImGui::EndPopup();
}

bool PeopleWindow::draw_new_password(const char* label, bool focus) {
    const float field = 240.0f * ui_scale_;
    table_row(label);
    ImGui::SetNextItemWidth(field);
    ImGui::InputText("##password", &password_, ImGuiInputTextFlags_Password);
    if (focus && ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(-1);
    table_row("Again");
    ImGui::SetNextItemWidth(field);
    ImGui::InputText("##again", &again_, ImGuiInputTextFlags_Password);
    const bool match = password_ == again_;
    if (!match && !again_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(kWarningColor, "Doesn't match");
    }
    return match;
}

bool PeopleWindow::buttons(const char* confirm, bool enabled) {
    if (!error_.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 380.0f * ui_scale_);
        ImGui::TextColored(kErrorColor, "%s", error_.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
    ImGui::BeginDisabled(!enabled);
    bool chosen = ImGui::Button(confirm);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || dialog_escape_pressed()) {
        close_dialog();
        return false;
    }
    if (enabled) {
        // Enter saves; a field being typed in has already taken its value.
        if (dialog_enter_pressed() && !ImGui::IsAnyItemActive()) chosen = true;
        ImGui::SameLine();
        ImGui::TextDisabled("Enter saves");
    }
    return chosen;
}

void PeopleWindow::draw_add() {
    const float field = 240.0f * ui_scale_;
    bool match = true;
    if (ImGui::BeginTable("##person", 2, ImGuiTableFlags_SizingFixedFit)) {
        table_row("Name");
        ImGui::SetNextItemWidth(field);
        ImGui::InputText("##name", &name_);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(-1);
        table_row("Role");
        ImGui::SetNextItemWidth(field);
        enum_combo("##role", role_, domain::kAllRoles);
        match = draw_new_password("Password");
        ImGui::EndTable();
    }
    ImGui::TextDisabled("%s", role_ == domain::Role::Admin ? "Admins need a password."
                                                           : "Brewers can go without a password.");

    if (!buttons("Add person", !is_blank(name_) && match)) return;
    try {
        users_.add(session_, name_, role_, password_);
        status_ = "Added " + name_ + ".";
        stale_ = true;
        close_dialog();
    } catch (const std::exception& e) {
        error_ = e.what();
    }
}

void PeopleWindow::draw_edit() {
    const domain::User* person = find(person_id_);
    if (!person) {
        close_dialog();
        return;
    }
    const domain::User before = *person;  // a copy: saving re-reads the list
    const bool me = before.id == session_.user_id;
    const float field = 240.0f * ui_scale_;
    bool match = true;
    if (ImGui::BeginTable("##person", 2, ImGuiTableFlags_SizingFixedFit)) {
        table_row("Name");
        ImGui::SetNextItemWidth(field);
        ImGui::InputText("##name", &name_);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(-1);

        table_row("Role");
        ImGui::BeginDisabled(me);
        ImGui::SetNextItemWidth(field);
        enum_combo("##role", role_, domain::kAllRoles);
        ImGui::EndDisabled();
        if (me) ImGui::SetItemTooltip("Another admin has to change your role.");

        table_row("Can sign in");
        ImGui::BeginDisabled(me);
        ImGui::Checkbox("##active", &active_);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", me ? "Another admin can make you inactive."
                                     : "Clear it when someone leaves; their records keep their name.");

        if (before.has_password) {
            table_row("Password");
            ImGui::Checkbox("Remove it", &remove_password_);
            if (!remove_password_) match = draw_new_password("New password");
        } else {
            match = draw_new_password("Password");
        }
        ImGui::EndTable();
    }
    if (!remove_password_) {
        ImGui::TextDisabled("%s", before.has_password ? "Leave the new password blank to keep the current one."
                                                      : "Leave it blank to go without one.");
    }

    if (!buttons("Save", !is_blank(name_) && match)) return;
    try {
        if (name_ != before.name) users_.rename(session_, before.id, name_);
        // A new password first, so someone can get one and be made an admin together...
        if (!remove_password_ && !password_.empty()) users_.set_password(session_, before.id, password_);
        if (role_ != before.role) users_.set_role(session_, before.id, role_);
        if (active_ != before.active) users_.set_active(session_, before.id, active_);
        // ...and a password removed last, after a change of role.
        if (remove_password_) users_.set_password(session_, before.id, "");
        status_ = "Saved " + name_ + ".";
        stale_ = true;
        close_dialog();
    } catch (const std::exception& e) {
        error_ = e.what();
        stale_ = true;  // the changes before the one refused were saved
    }
}

void PeopleWindow::draw_own_password() {
    const domain::User* me = find(session_.user_id);
    const bool has_password = me && me->has_password;
    const float field = 240.0f * ui_scale_;
    bool match = true;
    if (ImGui::BeginTable("##own_password", 2, ImGuiTableFlags_SizingFixedFit)) {
        if (has_password) {
            table_row("Current password");
            ImGui::SetNextItemWidth(field);
            ImGui::InputText("##current", &current_password_, ImGuiInputTextFlags_Password);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(-1);
        }
        match = draw_new_password(has_password ? "New password" : "Password", !has_password);
        ImGui::EndTable();
    }
    if (has_password) ImGui::TextDisabled("Leave the new password blank to remove it.");

    if (!buttons("Change password", match && (has_password || !password_.empty()))) return;
    try {
        users_.change_own_password(session_, current_password_, password_);
        status_ = password_.empty() ? "Your password is removed." : "Your password is changed.";
        stale_ = true;
        close_dialog();
    } catch (const std::exception& e) {
        error_ = e.what();
    }
}

} // namespace ninkasi::ui
