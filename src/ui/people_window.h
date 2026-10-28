#pragma once
// File > People, for admins: who uses Ninkasi, their roles and passwords,
// adding people, and making them inactive when they leave. Also the "Change
// my password" dialog anyone can open from their name in the menu bar.

#include "app/session.h"
#include "app/user_service.h"
#include "domain/user.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ninkasi::ui {

class PeopleWindow {
public:
    PeopleWindow(app::UserService& users, const app::Session& session, float ui_scale);

    void show();
    // Opens the dialog for changing the signed-in person's own password.
    void change_my_password();
    // Draws the window, if it's open, and any dialog opened from it.
    void draw();

private:
    enum class Dialog { None, Add, Edit, OwnPassword };

    void draw_list();
    void draw_dialog();
    void draw_add();
    void draw_edit();
    void draw_own_password();
    // "Password" and "Again" rows; whether the two match. `focus` puts the
    // keyboard in the first when the dialog opens.
    bool draw_new_password(const char* label, bool focus = false);
    // Save and Cancel, with Enter and Escape; whether Save was chosen.
    bool buttons(const char* confirm, bool enabled);
    void open_dialog(Dialog dialog, std::int64_t person_id = 0);
    void close_dialog();
    const domain::User* find(std::int64_t id) const;

    app::UserService& users_;
    const app::Session& session_;
    float ui_scale_;

    bool visible_ = false;
    bool focus_ = false;
    std::vector<domain::User> people_;  // re-read after every change
    bool stale_ = true;
    std::string status_;

    // The dialog being shown, and what's typed in it.
    Dialog dialog_ = Dialog::None;
    bool open_popup_ = false;
    std::int64_t person_id_ = 0;  // being edited
    std::string name_;
    domain::Role role_ = domain::Role::Brewer;
    bool active_ = true;
    bool remove_password_ = false;
    std::string current_password_;
    std::string password_;
    std::string again_;
    std::string error_;
};

} // namespace ninkasi::ui
