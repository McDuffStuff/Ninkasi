#pragma once
// How each kind of equipment is shown, shared by the floor and the element
// windows: its accent color, so a fermenter looks like a fermenter
// everywhere, and whether it has a log.

#include "domain/element.h"

#include <imgui.h>

namespace ninkasi::ui {

ImVec4 element_accent(domain::ElementType type);

// Whether equipment has a Log tab of what happened to it (and takes notes).
// Inventory, packaged goods and the keg cleaner keep their own logs of stock
// in and out instead, and a recipe station has nothing to log.
bool has_activity_log(domain::ElementType type);

} // namespace ninkasi::ui
