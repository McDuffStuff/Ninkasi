#pragma once
// The sign-in screen. Ninkasi shows it at startup once more than one person
// uses it, or anyone has a password: choose your name, type your password if
// you have one, and press Enter.

#include "app/session.h"
#include "app/user_service.h"
#include "domain/user.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ninkasi::ui {

class SignInScreen {
public:
    SignInScreen(app::UserService& users, float ui_scale);

    // Draws the screen over the whole main window. Returns the session in the
    // frame someone signs in.
    std::optional<app::Session> draw();

private:
    void choose(std::int64_t id);
    const domain::User* chosen() const;

    app::UserService& users_;
    float ui_scale_;
    std::vector<domain::User> people_;  // who can sign in
    std::int64_t chosen_ = 0;
    std::string password_;
    std::string error_;
    bool focus_password_ = false;
};

} // namespace ninkasi::ui
