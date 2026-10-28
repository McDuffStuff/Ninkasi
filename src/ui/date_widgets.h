#pragma once
// Date and time pickers.

#include "domain/timestamp.h"

namespace ninkasi::ui {

// A button showing the date; clicking it opens a month calendar. Returns
// true when the brewer picks a date.
bool date_picker(const char* id, domain::Date& date);

// A moment in local time: a date picker, hour and minute fields and a "Now"
// button. Returns true when the time changes.
bool time_picker(const char* id, domain::Timestamp& time);

} // namespace ninkasi::ui
