#pragma once
// The sign-in screen. Ninkasi shows it at startup once more than one person
// uses it, or anyone has a password: choose your name, type your password if
// you have one, and press Enter. Someone who forgot their password can choose
// a new one here with their recovery code.

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
    enum class Mode {
        SignIn,   // name and password
        Recover,  // recovery code and a new password
        NewCode,  // the recovery code that replaces the one just used
    };

    std::optional<app::Session> draw_sign_in(float width);
    void draw_recover(float width);
    std::optional<app::Session> draw_new_code();
    void choose(std::int64_t id);
    // Switches to `mode` with empty fields.
    void start(Mode mode);
    const domain::User* chosen() const;

    app::UserService& users_;
    float ui_scale_;
    std::vector<domain::User> people_;  // who can sign in
    std::int64_t chosen_ = 0;
    Mode mode_ = Mode::SignIn;
    std::string password_;
    std::string again_;  // a new password, typed again
    std::string code_;   // a recovery code
    std::string error_;
    bool focus_ = false;  // puts the keyboard in the first field
    std::optional<app::PasswordRecovery> recovered_;  // signed in once its new code is noted
};

} // namespace ninkasi::ui
