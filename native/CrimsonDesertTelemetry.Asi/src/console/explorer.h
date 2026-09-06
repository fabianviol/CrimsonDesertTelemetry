#pragma once
#include "common.h"

namespace ch { namespace explorer {

// Deletes any stale command file and starts the poll thread.
void Start();
void Stop();

// Runs one command line and writes its output through Out(). Exposed so the
// command set can be exercised without the file channel.
void Execute(const std::string& line);

}}  // namespace ch::explorer

