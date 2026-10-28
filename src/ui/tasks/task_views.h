#pragma once
// Tasks on screen: the Tasks window (every open task, grouped by when it's
// due), each piece of equipment's task list, and the task editor.

#include "app/batch_service.h"
#include "app/floor_service.h"
#include "app/task_service.h"
#include "domain/task.h"
#include "domain/timestamp.h"

#include <imgui.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ninkasi::ui {

class TaskViews {
public:
    TaskViews(app::TaskService& tasks, const app::FloorService& floor, const app::BatchService& batches,
              float ui_scale);

    void show_window();
    bool* window_visible() { return &visible_; }

    // Call once per frame, outside any window: the Tasks window and the editor.
    void draw();

    // An element's open tasks and a form to add one (for its Tasks tab).
    void draw_element_tasks(std::int64_t element_id);

private:
    // A task being written in an "add" form.
    struct Draft {
        std::string title;
        bool has_due = false;
        domain::Date due = domain::today();
        std::int64_t element_id = 0;  // 0: not for any equipment
    };

    void draw_window();
    void draw_editor();
    // A row per task: a checkbox to complete it, its title (click to edit),
    // its equipment and when it's due.
    void draw_rows(const char* id, const std::vector<domain::Task>& tasks, bool show_element);
    // Title, due date (and equipment, unless fixed) and an Add button.
    void draw_add_form(Draft& draft, std::optional<std::int64_t> fixed_element);
    // The equipment a task can be for, with a "none" choice.
    void element_combo(const char* label, std::int64_t& element_id);
    void edit(const domain::Task& task);

    app::TaskService& tasks_;
    const app::FloorService& floor_;
    const app::BatchService& batches_;
    float ui_scale_;

    bool visible_ = false;
    bool focus_ = false;
    Draft window_draft_;
    std::unordered_map<std::int64_t, Draft> element_drafts_;
    std::string add_error_;

    // Recently done tasks, re-read when tasks change.
    std::vector<domain::Task> done_;
    std::uint64_t done_revision_ = 0;
    bool done_loaded_ = false;

    // The editor.
    bool open_editor_ = false;
    ImVec2 editor_center_{};
    domain::Task editing_;
    bool editing_has_due_ = false;
    domain::Date editing_due_{};
    std::int64_t editing_element_ = 0;
    std::string editor_error_;
};

} // namespace ninkasi::ui
