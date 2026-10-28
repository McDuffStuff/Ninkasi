#include "ui/recipes/recipe_library_window.h"

#include "app/beerjson_import.h"
#include "ui/file_dialog.h"
#include "ui/widgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <sstream>

namespace ninkasi::ui {

namespace {

constexpr const char* kNewPopup = "New recipe";
constexpr const char* kDeletePopup = "Delete recipe?";
constexpr const char* kImportPopup = "BeerJSON import";

bool matches(const data::RecipeSummary& recipe, const std::string& filter) {
    if (filter.empty()) return true;
    auto lower = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    };
    const std::string needle = lower(filter);
    return lower(recipe.name).find(needle) != std::string::npos ||
           lower(recipe.style).find(needle) != std::string::npos;
}

// "2026-10-27T14:03:05Z" -> "2026-10-27 14:03"
std::string short_timestamp(const std::string& iso) {
    if (iso.size() < 16) return iso;
    std::string text = iso.substr(0, 16);
    text[10] = ' ';
    return text;
}

std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

} // namespace

RecipeLibraryWindow::RecipeLibraryWindow(app::RecipeService& recipes, app::IngredientLibrary& library,
                                         RecipeEditors& editors, float ui_scale)
    : recipes_(recipes), library_(library), editors_(editors), ui_scale_(ui_scale) {}

void RecipeLibraryWindow::show() {
    visible_ = true;
    focus_ = true;
    refresh();
}

void RecipeLibraryWindow::refresh() {
    list_ = recipes_.list();
    if (selected_ && std::none_of(list_.begin(), list_.end(), [&](const auto& r) { return r.id == *selected_; })) {
        selected_.reset();
    }
}

void RecipeLibraryWindow::draw() {
    // Dialogs can be raised by an import even while the list is hidden.
    if (!visible_ && !open_import_report_) return;

    if (visible_) {
        ImGui::SetNextWindowSize(ImVec2(620.0f, 460.0f) * ui_scale_, ImGuiCond_FirstUseEver);
        bool raise = false;
        if (focus_) {
            ImGui::SetNextWindowFocus();
            focus_ = false;
            raise = true;
        }
        const bool expanded = ImGui::Begin("Recipes", &visible_);
        if (raise) raise_os_window();
        if (expanded) {
            draw_list();
            draw_new_recipe_popup();
            draw_delete_confirmation();
            draw_import_report();
        }
        ImGui::End();
    } else {
        draw_import_report();
    }
}

void RecipeLibraryWindow::draw_list() {
    if (ImGui::Button("New recipe")) {
        new_name_.clear();
        open_new_popup_ = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!selected_);
    if (ImGui::Button("Open")) editors_.open(*selected_);
    ImGui::SameLine();
    if (ImGui::Button("Duplicate")) {
        selected_ = recipes_.duplicate(*selected_);
        refresh();
        editors_.open(*selected_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete...")) {
        delete_id_ = *selected_;
        open_delete_popup_ = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Import BeerJSON...")) {
        import_files(open_files_dialog("Import BeerJSON recipes and water profiles", "BeerJSON files", "*.json"));
    }
    ImGui::SetItemTooltip("You can also drop BeerJSON files onto Ninkasi");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Search", &filter_);

    if (list_.empty()) {
        ImGui::Spacing();
        wrapped_note("No recipes yet. Create one, or import BeerJSON files exported from other brewing software "
                     "(you can drop them onto Ninkasi).");
        return;
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("##recipes", 3, flags)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("Style", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn("Changed (UTC)", ImGuiTableColumnFlags_WidthFixed, 130.0f * ui_scale_);
    ImGui::TableHeadersRow();
    for (const data::RecipeSummary& recipe : list_) {
        if (!matches(recipe, filter_)) continue;
        ImGui::PushID(static_cast<int>(recipe.id));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const bool is_selected = selected_ == recipe.id;
        if (ImGui::Selectable(recipe.name.c_str(), is_selected,
                              ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
            selected_ = recipe.id;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) editors_.open(recipe.id);
        }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(recipe.style.c_str());
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", short_timestamp(recipe.updated_at).c_str());
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void RecipeLibraryWindow::draw_new_recipe_popup() {
    if (open_new_popup_) {
        ImGui::OpenPopup(kNewPopup);
        open_new_popup_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
    if (!ImGui::BeginPopupModal(kNewPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(320.0f * ui_scale_);
    const bool entered =
        ImGui::InputTextWithHint("##name", "Recipe name", &new_name_, ImGuiInputTextFlags_EnterReturnsTrue);
    const bool blank = new_name_.find_first_not_of(" \t") == std::string::npos;
    ImGui::TextDisabled("Starts from your new-recipe defaults (Recipes > Brewing settings).");

    ImGui::BeginDisabled(blank);
    if (ImGui::Button("Create") || (entered && !blank)) {
        const std::int64_t id = recipes_.create(recipes_.new_recipe(new_name_));
        refresh();
        selected_ = id;
        editors_.open(id);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void RecipeLibraryWindow::draw_delete_confirmation() {
    if (open_delete_popup_) {
        ImGui::OpenPopup(kDeletePopup);
        open_delete_popup_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
    if (!ImGui::BeginPopupModal(kDeletePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    const auto recipe = std::find_if(list_.begin(), list_.end(), [&](const auto& r) { return r.id == delete_id_; });
    if (recipe == list_.end()) {
        ImGui::CloseCurrentPopup();
    } else {
        ImGui::Text("Delete the recipe \"%s\"?", recipe->name.c_str());
        ImGui::TextDisabled("This can't be undone.");
        ImGui::Spacing();
        if (ImGui::Button("Delete")) {
            editors_.discard(delete_id_);
            recipes_.remove(delete_id_);
            refresh();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void RecipeLibraryWindow::import_files(const std::vector<std::filesystem::path>& files) {
    if (files.empty()) return;
    import_report_.clear();
    const domain::Recipe defaults = recipes_.defaults();
    std::optional<std::int64_t> last_imported;

    for (const std::filesystem::path& file : files) {
        const std::string file_name = file.filename().string();
        const std::optional<std::string> text = read_file(file);
        if (!text) {
            import_report_.push_back(file_name + ": couldn't read the file");
            continue;
        }
        try {
            const app::BeerJsonImport result = app::import_beerjson(*text, defaults);
            for (const domain::Recipe& recipe : result.recipes) {
                last_imported = recipes_.create(recipe);
                import_report_.push_back("Imported \"" + recipe.name + "\" from " + file_name);
            }
            for (const domain::WaterSource& water : result.waters) {
                library_.add(water);
                import_report_.push_back("Added the water profile \"" + water.name + "\" to the ingredient library");
            }
            for (const std::string& warning : result.warnings) import_report_.push_back("    " + warning);
            if (result.recipes.empty() && result.waters.empty()) {
                import_report_.push_back(file_name + ": no recipes or water profiles in the file");
            }
        } catch (const std::exception& e) {
            import_report_.push_back(file_name + ": " + e.what());
        }
    }

    show();
    if (last_imported) selected_ = last_imported;
    open_import_report_ = true;
}

void RecipeLibraryWindow::draw_import_report() {
    if (open_import_report_ && !ImGui::IsPopupOpen(kImportPopup)) ImGui::OpenPopup(kImportPopup);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::SetNextWindowSizeConstraints(ImVec2(420.0f, 0.0f) * ui_scale_, ImVec2(900.0f, 600.0f) * ui_scale_);
    if (!ImGui::BeginPopupModal(kImportPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    for (const std::string& line : import_report_) {
        if (line.starts_with("    ")) {
            ImGui::TextDisabled("%s", line.c_str());
        } else {
            ImGui::TextUnformatted(line.c_str());
        }
    }
    ImGui::Spacing();
    if (ImGui::Button("OK") || dialog_enter_pressed() || dialog_escape_pressed()) {
        open_import_report_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace ninkasi::ui
