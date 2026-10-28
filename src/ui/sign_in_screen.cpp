#include "ui/sign_in_screen.h"

#include "ui/display.h"
#include "ui/fonts.h"
#include "ui/search_combo.h"
#include "ui/widgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <exception>
#include <utility>

namespace ninkasi::ui {

SignInScreen::SignInScreen(app::UserService& users, float ui_scale) : users_(users), ui_scale_(ui_scale) {
    for (domain::User& person : users_.people()) {
        if (person.active) people_.push_back(std::move(person));
    }
    // Whoever signed in last is the likeliest to be back.
    const std::optional<std::int64_t> last = users_.last_signed_in();
    const bool last_is_here = last && std::any_of(people_.begin(), people_.end(),
                                                  [&](const domain::User& person) { return person.id == *last; });
    if (last_is_here) {
        choose(*last);
    } else if (!people_.empty()) {
        choose(people_.front().id);
    }
}

void SignInScreen::choose(std::int64_t id) {
    chosen_ = id;
    start(Mode::SignIn);
}

void SignInScreen::start(Mode mode) {
    mode_ = mode;
    password_.clear();
    again_.clear();
    code_.clear();
    error_.clear();
    focus_ = true;
}

const domain::User* SignInScreen::chosen() const {
    const auto found = std::find_if(people_.begin(), people_.end(),
                                    [this](const domain::User& person) { return person.id == chosen_; });
    return found == people_.end() ? nullptr : &*found;
}

std::optional<app::Session> SignInScreen::draw() {
    // One undecorated window covering the main window.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
    std::optional<app::Session> signed_in;
    if (!ImGui::Begin("Sign in##ninkasi", nullptr, kFlags)) {
        ImGui::End();
        return signed_in;
    }

    // A column in the middle, a little above center.
    const float width = 340.0f * ui_scale_;
    const ImVec2 room = ImGui::GetContentRegionAvail();
    ImGui::SetCursorPos(ImVec2(std::max(0.0f, (room.x - width) * 0.5f), std::max(0.0f, room.y * 0.3f)));
    ImGui::BeginGroup();
    ImGui::PushFont(heading_font());
    ImGui::TextUnformatted("Ninkasi");
    ImGui::PopFont();
    ImGui::Spacing();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    switch (mode_) {
    case Mode::SignIn:  signed_in = draw_sign_in(width); break;
    case Mode::Recover: draw_recover(width); break;
    case Mode::NewCode: signed_in = draw_new_code(); break;
    }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();

    ImGui::End();
    return signed_in;
}

std::optional<app::Session> SignInScreen::draw_sign_in(float width) {
    ImGui::TextUnformatted("Who's signing in?");
    const domain::User* person = chosen();
    ImGui::SetNextItemWidth(width);
    if (begin_search_combo("##who", person ? person->name.c_str() : "Choose your name")) {
        std::vector<std::string> names;
        for (const domain::User& someone : people_) names.push_back(someone.name);
        for (std::size_t i : search_order(names)) {
            ImGui::PushID(static_cast<int>(people_[i].id));
            if (search_item(names[i].c_str(), people_[i].id == chosen_)) choose(people_[i].id);
            ImGui::PopID();
        }
        end_search_combo();
    }
    person = chosen();

    bool entered = false;
    if (person && person->has_password) {
        ImGui::Spacing();
        ImGui::TextUnformatted("Password");
        ImGui::SetNextItemWidth(width);
        if (focus_) {
            ImGui::SetKeyboardFocusHere();
            focus_ = false;
        }
        entered = ImGui::InputText("##password", &password_,
                                   ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(!person);
    const bool clicked = ImGui::Button("Sign in");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("Enter signs in");
    std::optional<app::Session> signed_in;
    if (person && (clicked || entered || (dialog_enter_pressed() && !ImGui::IsAnyItemActive()))) {
        signed_in = users_.sign_in(person->id, password_);
        if (!signed_in) {
            error_ = "That password isn't right.";
            password_.clear();
            focus_ = true;
        }
    }
    if (!error_.empty()) ImGui::TextColored(kErrorColor, "%s", error_.c_str());

    if (person && person->has_password) {
        ImGui::Spacing();
        ImGui::Spacing();
        if (ImGui::TextLink("Forgot your password?")) start(Mode::Recover);
    }
    return signed_in;
}

void SignInScreen::draw_recover(float width) {
    const domain::User* person = chosen();
    if (!person) {
        start(Mode::SignIn);
        return;
    }
    ImGui::TextUnformatted(("Forgot your password, " + person->name + "?").c_str());
    ImGui::Spacing();

    if (!person->has_recovery_code) {
        ImGui::TextDisabled("You don't have a recovery code. Ask an admin to set a new password for you in "
                            "File > People.");
        ImGui::Spacing();
        if (ImGui::Button("Back") || dialog_escape_pressed()) start(Mode::SignIn);
        return;
    }

    ImGui::TextDisabled("Enter the recovery code you were given when you set your password, and choose a new "
                        "password.");
    ImGui::Spacing();
    ImGui::TextUnformatted("Recovery code");
    ImGui::SetNextItemWidth(width);
    if (focus_) {
        ImGui::SetKeyboardFocusHere();
        focus_ = false;
    }
    ImGui::InputTextWithHint("##code", "XXXX-XXXX-XXXX-XXXX-XXXX", &code_, ImGuiInputTextFlags_CharsUppercase);
    ImGui::TextUnformatted("New password");
    ImGui::SetNextItemWidth(width);
    ImGui::InputText("##new_password", &password_, ImGuiInputTextFlags_Password);
    ImGui::TextUnformatted("Again");
    ImGui::SetNextItemWidth(width);
    ImGui::InputText("##again", &again_, ImGuiInputTextFlags_Password);
    const bool match = password_ == again_;
    if (!match && !again_.empty()) ImGui::TextColored(kWarningColor, "The passwords don't match.");

    ImGui::Spacing();
    const bool ready = !code_.empty() && !password_.empty() && match;
    ImGui::BeginDisabled(!ready);
    bool submit = ImGui::Button("Set password and sign in");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Back") || dialog_escape_pressed()) {
        start(Mode::SignIn);
        return;
    }
    // Enter submits; a field being typed in has already taken its value.
    if (ready && dialog_enter_pressed() && !ImGui::IsAnyItemActive()) submit = true;
    if (submit) {
        try {
            recovered_ = users_.recover(person->id, code_, password_);
            if (recovered_) {
                start(Mode::NewCode);
            } else {
                error_ = "That recovery code isn't right.";
            }
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    if (!error_.empty()) ImGui::TextColored(kErrorColor, "%s", error_.c_str());
}

std::optional<app::Session> SignInScreen::draw_new_code() {
    if (!recovered_) {
        start(Mode::SignIn);
        return std::nullopt;
    }
    ImGui::TextUnformatted("Your password is changed.");
    ImGui::Spacing();
    ImGui::TextDisabled("Your recovery code is used up, so here is a new one. Write it down and keep it somewhere "
                        "safe; it's shown only now.");
    ImGui::Spacing();
    show_recovery_code(recovered_->new_code);
    ImGui::Spacing();
    if (!ImGui::Button("I've written it down")) return std::nullopt;
    std::optional<app::Session> session = recovered_->session;
    recovered_.reset();
    return session;
}

} // namespace ninkasi::ui
