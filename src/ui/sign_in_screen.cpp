#include "ui/sign_in_screen.h"

#include "ui/display.h"
#include "ui/fonts.h"
#include "ui/search_combo.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
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
    password_.clear();
    error_.clear();
    focus_password_ = true;
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
    const float width = 320.0f * ui_scale_;
    const ImVec2 room = ImGui::GetContentRegionAvail();
    ImGui::SetCursorPos(ImVec2(std::max(0.0f, (room.x - width) * 0.5f), std::max(0.0f, room.y * 0.3f)));
    ImGui::BeginGroup();
    ImGui::PushFont(heading_font());
    ImGui::TextUnformatted("Ninkasi");
    ImGui::PopFont();
    ImGui::Spacing();

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
        if (focus_password_) {
            ImGui::SetKeyboardFocusHere();
            focus_password_ = false;
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
    if (person && (clicked || entered || (dialog_enter_pressed() && !ImGui::IsAnyItemActive()))) {
        signed_in = users_.sign_in(person->id, password_);
        if (!signed_in) {
            error_ = "That password isn't right.";
            password_.clear();
            focus_password_ = true;
        }
    }
    if (!error_.empty()) ImGui::TextColored(kErrorColor, "%s", error_.c_str());

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ImGui::TextDisabled("Forgot your password? An admin can set a new one in File > People.");
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();

    ImGui::End();
    return signed_in;
}

} // namespace ninkasi::ui
