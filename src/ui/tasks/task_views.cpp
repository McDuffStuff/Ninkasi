#include "ui/tasks/task_views.h"

#include "ui/date_widgets.h"
#include "ui/display.h"
#include "ui/widgets.h"

#include <imgui_stdlib.h>

#include <exception>

namespace ninkasi::ui {

namespace {

constexpr const char* kEditorPopup = "Edit task";

constexpr ImGuiTableFlags kListFlags =
    ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;

bool is_blank(const std::string& text) {
    return text.find_first_not_of(" \t\r\n") == std::string::npos;
}

} // namespace

TaskViews::TaskViews(app::TaskService& tasks, const app::FloorService& floor, const app::BatchService& batches,
                     float ui_scale)
    : tasks_(tasks), floor_(floor), batches_(batches), ui_scale_(ui_scale) {}

void TaskViews::show_window() {
    visible_ = true;
    focus_ = true;
}

void TaskViews::draw() {
    if (visible_) draw_window();
    draw_editor();
}

// --- The Tasks window -----------------------------------------------------------

void TaskViews::draw_window() {
    // First shown on the right of the main window, clear of the batch list.
    const ImGuiViewport* main = ImGui::GetMainViewport();
    const ImVec2 top_right(main->WorkPos.x + main->WorkSize.x - 40.0f * ui_scale_, main->WorkPos.y + 80.0f * ui_scale_);
    ImGui::SetNextWindowPos(top_right, ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(680.0f, 520.0f) * ui_scale_, ImGuiCond_FirstUseEver);
    if (focus_) {
        ImGui::SetNextWindowFocus();
        focus_ = false;
    }
    if (!ImGui::Begin("Tasks", &visible_)) {
        ImGui::End();
        return;
    }

    draw_add_form(window_draft_, std::nullopt);
    ImGui::Spacing();

    // Open tasks by when they're due. Tasks of equipment that was just
    // removed are skipped until the list is next re-read.
    const domain::Date today = domain::today();
    std::vector<domain::Task> overdue, due_today, upcoming, undated;
    for (const domain::Task& task : tasks_.open_tasks()) {
        if (task.element_id && !floor_.find(*task.element_id)) continue;
        switch (domain::urgency(task, today)) {
        case domain::TaskUrgency::Overdue:  overdue.push_back(task); break;
        case domain::TaskUrgency::DueToday: due_today.push_back(task); break;
        case domain::TaskUrgency::Upcoming: upcoming.push_back(task); break;
        case domain::TaskUrgency::None:     undated.push_back(task); break;
        }
    }
    if (overdue.empty() && due_today.empty() && upcoming.empty() && undated.empty()) {
        wrapped_note("Nothing to do. Add a task above, or from a tank's Tasks tab.");
    }
    const std::pair<const char*, const std::vector<domain::Task>*> groups[] = {
        {"Overdue", &overdue}, {"Today", &due_today}, {"Upcoming", &upcoming}, {"No due date", &undated}};
    for (const auto& [title, list] : groups) {
        if (list->empty()) continue;
        ImGui::SeparatorText(title);
        draw_rows(title, *list, true);
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Recently done")) {
        if (!done_loaded_ || done_revision_ != tasks_.revision()) {
            done_ = tasks_.recently_done(std::nullopt, 30);
            done_revision_ = tasks_.revision();
            done_loaded_ = true;
        }
        if (done_.empty()) {
            wrapped_note("Completed tasks will show here.");
        } else {
            draw_rows("##done", done_, true);
        }
    }
    ImGui::End();
}

void TaskViews::draw_element_tasks(std::int64_t element_id) {
    draw_add_form(element_drafts_[element_id], element_id);
    ImGui::Spacing();
    const std::vector<domain::Task> open = tasks_.open_for(element_id);
    if (open.empty()) {
        wrapped_note("No open tasks for this equipment.");
    } else {
        draw_rows("##open", open, false);
    }
}

void TaskViews::draw_rows(const char* id, const std::vector<domain::Task>& tasks, bool show_element) {
    const domain::Date today = domain::today();
    if (!ImGui::BeginTable(id, show_element ? 4 : 3, kListFlags)) return;
    ImGui::TableSetupColumn("##done");
    ImGui::TableSetupColumn("##title", ImGuiTableColumnFlags_WidthStretch);
    if (show_element) ImGui::TableSetupColumn("##element");
    ImGui::TableSetupColumn("##due");

    for (const domain::Task& task : tasks) {
        ImGui::PushID(static_cast<int>(task.id));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        bool done = task.done();
        if (ImGui::Checkbox("##done", &done)) {
            if (done) {
                tasks_.complete(task.id, domain::now());
            } else {
                tasks_.reopen(task.id);
            }
        }
        ImGui::SetItemTooltip("%s", done ? "Mark as not done" : "Mark as done");

        ImGui::TableNextColumn();
        if (ImGui::Selectable(task.title.c_str())) edit(task);
        if (!task.notes.empty()) ImGui::SetItemTooltip("%s", task.notes.c_str());

        if (show_element) {
            ImGui::TableNextColumn();
            if (const domain::Element* element = task.element_id ? floor_.find(*task.element_id) : nullptr) {
                ImGui::TextDisabled("%s", element->name.c_str());
            }
        }

        ImGui::TableNextColumn();
        if (task.done_at) {
            ImGui::TextDisabled("Done %s", date_text(domain::local_date(*task.done_at)).c_str());
        } else if (task.due) {
            const std::string due = due_text(*task.due, today);
            switch (domain::urgency(task, today)) {
            case domain::TaskUrgency::Overdue:  ImGui::TextColored(kErrorColor, "%s", due.c_str()); break;
            case domain::TaskUrgency::DueToday: ImGui::TextColored(kWarningColor, "%s", due.c_str()); break;
            default:                            ImGui::TextUnformatted(due.c_str()); break;
            }
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void TaskViews::draw_add_form(Draft& draft, std::optional<std::int64_t> fixed_element) {
    ImGui::PushID(&draft);
    ImGui::SetNextItemWidth(240.0f * ui_scale_);
    const bool entered = ImGui::InputTextWithHint("##title", "New task...", &draft.title,
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::Checkbox("Due", &draft.has_due);
    if (draft.has_due) {
        ImGui::SameLine();
        date_picker("##due", draft.due);
    }
    if (!fixed_element) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f * ui_scale_);
        element_combo("##element", draft.element_id);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(is_blank(draft.title));
    const bool add = ImGui::Button("Add") || (entered && !is_blank(draft.title));
    ImGui::EndDisabled();

    if (add) {
        domain::Task task;
        task.title = draft.title;
        if (draft.has_due) task.due = draft.due;
        // A task for a tank also belongs to the batch in it (or being brewed on it).
        if (const std::int64_t element = fixed_element.value_or(draft.element_id); element != 0) {
            task.element_id = element;
            if (const std::optional<app::VesselContents> held = batches_.contents(element)) {
                task.batch_id = held->batch_id;
            } else if (const domain::Brew* brew = batches_.brew_at(element)) {
                task.batch_id = brew->batch_id;
            }
        }
        try {
            tasks_.add(task);
            draft.title.clear();
            add_error_.clear();
        } catch (const std::exception& e) {
            add_error_ = e.what();
        }
    }
    if (!add_error_.empty()) ImGui::TextColored(kErrorColor, "%s", add_error_.c_str());
    ImGui::PopID();
}

void TaskViews::element_combo(const char* label, std::int64_t& element_id) {
    const domain::Element* chosen = element_id ? floor_.find(element_id) : nullptr;
    if (!ImGui::BeginCombo(label, chosen ? chosen->name.c_str() : "No equipment")) return;
    if (ImGui::Selectable("No equipment", !chosen)) element_id = 0;
    for (const domain::Element& element : floor_.elements()) {
        ImGui::PushID(static_cast<int>(element.id));
        if (ImGui::Selectable(element.name.c_str(), element.id == element_id)) element_id = element.id;
        ImGui::PopID();
    }
    ImGui::EndCombo();
}

// --- The editor -----------------------------------------------------------------

void TaskViews::edit(const domain::Task& task) {
    editing_ = task;
    editing_has_due_ = task.due.has_value();
    editing_due_ = task.due.value_or(domain::today());
    editing_element_ = task.element_id.value_or(0);
    editor_error_.clear();
    open_editor_ = true;
    editor_center_ = ImGui::GetWindowViewport()->GetCenter();
}

void TaskViews::draw_editor() {
    if (open_editor_) {
        ImGui::OpenPopup(kEditorPopup);
        open_editor_ = false;
    }
    ImGui::SetNextWindowPos(editor_center_, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(kEditorPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    if (ImGui::BeginTable("##task", 2, ImGuiTableFlags_SizingFixedFit)) {
        table_row("Title");
        ImGui::SetNextItemWidth(320.0f * ui_scale_);
        ImGui::InputText("##title", &editing_.title);
        table_row("Notes");
        ImGui::InputTextMultiline("##notes", &editing_.notes, ImVec2(320.0f, 80.0f) * ui_scale_);
        table_row("Equipment");
        ImGui::SetNextItemWidth(200.0f * ui_scale_);
        element_combo("##element", editing_element_);
        table_row("Due");
        ImGui::Checkbox("##has_due", &editing_has_due_);
        if (editing_has_due_) {
            ImGui::SameLine();
            date_picker("##due", editing_due_);
        }
        ImGui::EndTable();
    }
    if (!editor_error_.empty()) ImGui::TextColored(kErrorColor, "%s", editor_error_.c_str());
    ImGui::Spacing();

    if (ImGui::Button("Save")) {
        editing_.due = editing_has_due_ ? std::optional<domain::Date>(editing_due_) : std::nullopt;
        if (editing_element_ != editing_.element_id.value_or(0)) {
            // Moved to other equipment: it now belongs to that tank's batch, if any.
            editing_.element_id = editing_element_ ? std::optional<std::int64_t>(editing_element_) : std::nullopt;
            editing_.batch_id.reset();
            if (const std::optional<app::VesselContents> held = batches_.contents(editing_element_)) {
                editing_.batch_id = held->batch_id;
            }
        }
        try {
            tasks_.update(editing_);
            ImGui::CloseCurrentPopup();
        } catch (const std::exception& e) {
            editor_error_ = e.what();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        tasks_.remove(editing_.id);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

} // namespace ninkasi::ui
