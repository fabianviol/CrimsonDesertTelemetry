#pragma once

namespace ch { namespace manylights {

// Explorer entry point: prepare, arm, status, read [label], reset, help.
void Command(const char* action, const char* label);
void Shutdown();

}}  // namespace ch::manylights

