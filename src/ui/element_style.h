#pragma once
// The accent color of each kind of equipment, shared by the floor and the
// element windows so a fermenter looks like a fermenter everywhere.

#include "domain/element.h"

#include <imgui.h>

namespace ninkasi::ui {

ImVec4 element_accent(domain::ElementType type);

} // namespace ninkasi::ui
